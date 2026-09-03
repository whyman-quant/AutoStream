#pragma once

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "comm/print.hpp"
#include "comm/velatools/thread_cpu_trace.h"
#include "comm/vela_third_party/readerwriterqueue.h"
#include "comm/velapex/chrono_utils.h"
#include "comm/velapex/spsc_queue.h"
#include "comm/velatools/datetime_utils.h"
#include "data/constant_space.h"
#include "data/data_and_types.h"
#include "data/runtime_policy.h"
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_manager.h"
#include "factors/_comm/factor_entry_registry.h"
#include "sdp_handler/strategy_interface.h"

// 时序线程内按 code_int 稀疏索引的槽位：持有该资产对应的 FactorEntryRowWriter（稀疏槽位覆盖至 constant_space::kMaxAssetCode）。
struct AssetState {
	factors::FactorEntryRowWriter* factor_entry_writer = nullptr;
};

// 时序因子计算线程：从本组资产的行情队列读取 TickDataInfo，驱动各 FactorEntryRowWriter 计算，
// 并通过 SPSC 队列向因子结果扫描线程投递完成通知。
// 每个线程负责一组资产与一个或多个时序因子集；每个资产对应一个 FactorEntryRowWriter。
class FactorCalculationThread {
public:
	//  默认构造函数，初始化线程对象为未关联线程的状态。
	FactorCalculationThread() : run_thread_(), scaler_(0), cpu_trace_ctx_(0) {}

	// thread_id：线程编号；factor_size：本线程输出的总因子个数；
	// off_set：本组资产在全局结果拼接中的起始行偏移；
	// code_list / factor_entry_names / factor_entry_config：资产列表、因子集及 EV 等配置；
	// factor_set_column_layout：引擎生成的全局列布局，RowWriter 按此将各时序集写入 result_cache 对应列；
	// factor_compute_time_points_map：各因子集计算触发时刻集合（kTimePoint 下与 TickDataInfo::trigger_time_ms 匹配）；
	// send_time_points_vector：需要向模型侧发送因子矩阵的时刻列表；
	// trigger_time_points_map：每个逻辑时刻的计算/发送/OnlySZ 等调度及统计上下文；
	// data_queue：本组资产的 TickDataInfo 队列；result_queue：向扫描线程投递「某时刻一批已完成」的通知；
	// result_cache：按时间戳缓存各资产因子字节块，供扫描线程拼装 SDP/input_t。
	FactorCalculationThread(int thread_id, int factor_size, size_t off_set, std::vector<std::string> code_list,
	    std::vector<std::string> factor_entry_names, factors::comm::FactorEntryConfig factor_entry_config,
	    factors::FactorSetColumnLayout factor_set_column_layout,
	    std::unordered_map<std::string, std::unordered_set<int>> factor_compute_time_points_map,
	    std::vector<int> send_time_points_vector,
	    std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map,
	    std::shared_ptr<moodycamel::ReaderWriterQueue<TickDataInfo>> data_queue,
	    std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>> result_queue,
	    std::shared_ptr<std::vector<std::vector<char>>> result_cache,
	    std::shared_ptr<std::vector<std::vector<unsigned char>>> readiness_cache)
	    : thread_id_(thread_id),
	      off_set_(off_set),
	      code_list_(code_list),
	      factor_entry_names_(factor_entry_names),
	      factor_entry_config_(std::move(factor_entry_config)),
	      factor_set_column_layout_(std::move(factor_set_column_layout)),
	      factor_compute_time_points_map_(std::move(factor_compute_time_points_map)),
	      send_time_points_vector_(std::move(send_time_points_vector)),
	      trigger_time_points_map_(std::move(trigger_time_points_map)),
	      data_queue_(std::move(data_queue)),
	      result_queue_(std::move(result_queue)),
	      result_cache_(std::move(result_cache)),
	      readiness_cache_(std::move(readiness_cache)),
	      row_factor_capacity_(static_cast<size_t>(factor_size)),
	      single_asset_raw_data_size_(factor_size * sizeof(factors::fval_t)),
	      single_asset_send_data_size_(sizeof(my_factor_double_v2) + single_asset_raw_data_size_),
	      scaler_(velapex::chrono_utils::RdtscTimer::GetScaler()),
	      cpu_trace_ctx_(scaler_, velatools::thread_cpu_trace::MakeTraceOptions(constant_space::kThreadCpuTraceReallocSettleSleepMs, constant_space::kThreadCpuTraceLoopSampleInterval, constant_space::kThreadCpuTraceWallSampleIntervalUs, constant_space::kThreadCpuTraceRunSegmentReserve)) {
		asset_states_.resize(constant_space::kMaxAssetCode + 1);
		group_code_ints_.reserve(code_list_.size());
		for (const auto& code_str : code_list_) {
			const int ci = std::atoi(code_str.c_str());
			group_code_ints_.push_back(ci);
			// 每资产一个 RowWriter：只持有本线程的时序因子集，写入时按 layout 散射到行内对应列
			asset_states_[ci].factor_entry_writer = new factors::FactorEntryRowWriter(
			    code_str, factor_entry_config_, factor_entry_names_, factor_set_column_layout_,
			    static_cast<size_t>(factor_size));
		}
		factor_compute_time_stats_info_list_.resize(factor_entry_names_.size());
		for (size_t i = 0; i < factor_entry_names_.size(); i++) {
			auto iter = factor_compute_time_points_map_.find(factor_entry_names_[i]);
			if (iter != factor_compute_time_points_map_.end()) {
				factor_compute_time_stats_info_list_[i].reserve(iter->second.size());
			} else {
				factor_compute_time_stats_info_list_[i].clear();
			}
		}
		WLOG(TO_STRING("[FactorCalculationThread] FactorCalculationThread #", thread_id, " constructed successfully."),
		    true);
	}

	// 禁用拷贝构造函数。
	FactorCalculationThread(const FactorCalculationThread&) = delete;

	// 禁用拷贝赋值函数。
	FactorCalculationThread& operator=(const FactorCalculationThread&) = delete;

	// 析构函数，确保线程安全退出并释放资源。
	~FactorCalculationThread() {
		Wait();
		for (int ci : group_code_ints_) {
			if (asset_states_[ci].factor_entry_writer != nullptr) {
				delete asset_states_[ci].factor_entry_writer;
				asset_states_[ci].factor_entry_writer = nullptr;
			}
		}
	}

	// 启动底层 std::thread，入口为 CalcFunc。
	void Start() { run_thread_ = std::move(std::thread(&FactorCalculationThread::CalcFunc, this)); }

	// 请求停止线程（非阻塞）。
	void Stop() noexcept { stop_flag_.store(true, std::memory_order_release); }

	// 等待线程结束（阻塞）。
	void Wait() {
		if (run_thread_.joinable()) {
			run_thread_.join();
			WLOG(cpu_trace_ctx_.record().FormatTraceLog(GetThreadOsTid()));
		}
	}

	// 是否已退出主循环（非阻塞）。
	bool IsStopped() const noexcept { return is_stopped_.load(std::memory_order_acquire); }

	// 本线程构造时传入的 thread_id。
	int GetThreadId() const { return thread_id_; }

	// 返回 run_thread_ 在 Linux 下的内核线程号（TID）；未启动或入口尚未执行时为 0。
	pid_t GetThreadOsTid() const noexcept {
		return run_thread_os_tid_.load(std::memory_order_acquire);
	}

	// A 点：刚进入 CalcFunc 时采样的逻辑 CPU；-1 表示尚未采样或 getcpu 失败。
	int GetThreadInitialOsCpu() const noexcept {
		return run_thread_initial_os_cpu_.load(std::memory_order_acquire);
	}

	// B 点：进入主循环前 sleep 后再采样的逻辑 CPU。
	int GetThreadReAllocateOsCpu() const noexcept {
		return run_thread_reallocate_os_cpu_.load(std::memory_order_acquire);
	}

	// 收集本线程内各 FactorEntry 指针到 vector_fep，供引擎汇总耗时统计。
	void CollectFactorEntryPtrs(std::vector<factors::comm::FactorEntryBase*>& vector_fep) const {
		for (int ci : group_code_ints_) {
			auto* p = asset_states_[ci].factor_entry_writer;
			if (p == nullptr) {
				continue;
			}
			auto ptrs = p->GetFactorEntryPtrs();
			vector_fep.insert(vector_fep.end(), ptrs.begin(), ptrs.end());
		}
	}

	// 本线程负责的因子集名称（逗号分隔），用于线程树和日志展示。
	std::string GetFactorSetNamesLabel() const {
		std::string s;
		for (size_t i = 0; i < factor_entry_names_.size(); ++i) {
			if (i != 0) {
				s += ", ";
			}
			s += factor_entry_names_[i];
		}
		return s;
	}

	const std::vector<std::vector<FactorComputeTimeStatsInfo>>& GetFactorComputeTimeStats() const {
		return factor_compute_time_stats_info_list_;
	}

	// 返回线程内各因子实例上报的额外线程（depth 相对本 FactorCalculationThread）。
	std::vector<std::pair<int, std::string>> CollectRuntimeThreadTreeLines() const {
		std::vector<std::pair<int, std::string>> lines;
		for (int ci : group_code_ints_) {
			const auto* fem = asset_states_[ci].factor_entry_writer;
			if (fem == nullptr) {
				continue;
			}
			auto factor_entry_ptrs = fem->GetFactorEntryPtrs();
			for (const auto* fe : factor_entry_ptrs) {
				if (!fe) {
					continue;
				}
				auto child_lines = fe->CollectRuntimeThreadTreeLines();
				for (const auto& child_line : child_lines) {
					lines.push_back(std::make_pair(child_line.first + 1, child_line.second));
				}
			}
			break;
		}
		return lines;
	}

private:
	// 因子计算线程的主逻辑函数。
	void CalcFunc() {
		cpu_trace_ctx_.OnThreadEntry(run_thread_os_tid_, run_thread_initial_os_cpu_, run_thread_reallocate_os_cpu_);
		TickDataInfo q;
		while (true) {
			if (data_queue_->try_dequeue(q)) {              // 从缓存中读取行情数据
				if (q.data_type == TickDataKind::kQuote) {  // 如果是 Tick（Quote）数据，则添加到因子计算器
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q1, 0, 1);
					__builtin_prefetch(q.q1->data, 0, 1);
					factors::FactorEntryRowWriter* fem = asset_states_[q.code_int].factor_entry_writer;
					fem->AddQuote(*q.q1->data);  // 添加Tick数据
					q.q1->Release(1);
					q.q1 = nullptr;
				} else if (q.data_type == TickDataKind::kTrans) {  // 如果是 Trans 数据，则添加到因子计算器
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q2, 0, 1);
					__builtin_prefetch(q.q2->data, 0, 1);
					factors::FactorEntryRowWriter* fem = asset_states_[q.code_int].factor_entry_writer;
					fem->AddTrans(*q.q2->data);  // 添加Trans数据
					q.q2->Release(1);
					q.q2 = nullptr;
				} else if (q.data_type == TickDataKind::kOrder) {  // 如果是 Order 数据，则添加到因子计算器
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q3, 0, 1);
					__builtin_prefetch(q.q3->data, 0, 1);
					factors::FactorEntryRowWriter* fem = asset_states_[q.code_int].factor_entry_writer;
					fem->AddOrder(*q.q3->data);  // 添加Order数据
					q.q3->Release(1);
					q.q3 = nullptr;
				} else if (q.data_type == TickDataKind::kTimePoint) {  // 如果是时间戳数据，则触发计算、发送结果
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
					const int factor_api_time_ms = q.data_time_ms;
#else
					const int factor_api_time_ms = q.trigger_time_ms;
#endif
					// 如果是计算时间戳，则触发计算
					if (q.time_operation & runtime_policy::TimeOperationBits::kCompute) {
						for (size_t factor_set_index = 0; factor_set_index < factor_entry_names_.size();
						     ++factor_set_index) {
							const auto& factor_entry_name = factor_entry_names_[factor_set_index];
							auto iter = factor_compute_time_points_map_.find(factor_entry_name);
							if (iter == factor_compute_time_points_map_.end()) {
								continue;
							}
							if (iter->second.find(q.trigger_time_ms) == iter->second.end()) {
								continue;
							}
							const uint64_t now_tsc = velapex::chrono_utils::RdtscTimer()();
							const double compute_tick_wait_elapsed_us =
							    static_cast<double>(now_tsc - q.start_time.rdtsc_num) * scaler_;
							const uint64_t start_timestamp_us =
							    static_cast<uint64_t>(velatools::datetime_utils::NowTimestampUs());
							// kCompute：仅触发 entry 内计算，不写 result_cache；落盘在下方 kSend 的 WriteAllOwnedFactorSetsInto
							for (int ci : group_code_ints_) {
								asset_states_[ci].factor_entry_writer->TriggerCompute(factor_api_time_ms,
								    factor_entry_name);
							}
							const uint64_t end_timestamp_us =
							    static_cast<uint64_t>(velatools::datetime_utils::NowTimestampUs());
							const uint64_t end_tsc = velapex::chrono_utils::RdtscTimer()();
							const double factor_calc_duration_us = static_cast<double>(end_tsc - now_tsc) * scaler_;
							factor_compute_time_stats_info_list_[factor_set_index].emplace_back();
							auto& row = factor_compute_time_stats_info_list_[factor_set_index].back();
							row.trigger_time_ms = q.trigger_time_ms;
							row.factor_set_name = factor_entry_name;
							row.start_timestamp_us = start_timestamp_us;
							row.end_timestamp_us = end_timestamp_us;
							row.tick_wait_duration_us = compute_tick_wait_elapsed_us;
							row.compute_duration_us = factor_calc_duration_us;
						}
					}
					// 如果是发送时间戳，则更新并发送结果
					if (q.time_operation & runtime_policy::TimeOperationBits::kSend) {
						// 按全局列布局将本线程持有的各时序因子集写入 result_cache 行（不覆盖截面列等其他区间）
						for (size_t i = 0; i < group_code_ints_.size(); ++i) {
							const int ci = group_code_ints_[i];
							auto* row = reinterpret_cast<factors::fval_t*>(
							    result_cache_->at(q.trigger_send_batch_idx).data() +
							    single_asset_send_data_size_ * (off_set_ + i) + sizeof(my_factor_double_v2));
							asset_states_[ci].factor_entry_writer->WriteAllOwnedFactorSetsInto(row,
							    row_factor_capacity_);
							if (readiness_cache_ != nullptr) {
								unsigned char* readiness_row = readiness_cache_->at(q.trigger_send_batch_idx).data() +
								    (off_set_ + i) * row_factor_capacity_;
								asset_states_[ci].factor_entry_writer->WriteAllReadinessInto(factor_api_time_ms,
								    readiness_row, row_factor_capacity_);
							}
						}
						// 通知扫描线程，该时间戳的结果已准备好
						while (!result_queue_->Push(q.trigger_send_batch_idx)) {
						}
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
						// 调用AfterUpdateFactors函数（open5m独有接口）
						// NOTE: 此处也可以放在TriggerCompte后，但是为了节约时间不影响及时发送，所以放在这里
						// 对于open5m的因子，计算时间戳和发送时间戳是相同的，所以AfterUpdateFactors放在这里也能保证每次计算后会调用该函数
						for (int ci : group_code_ints_) {
							asset_states_[ci].factor_entry_writer->AfterUpdateFactors(factor_api_time_ms);
						}
#endif
					}
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
					// 如果是调用OnGlobalTime函数，则调用OnGlobalTime函数
					if (q.time_operation & runtime_policy::TimeOperationBits::kGlobalTime) {
						for (int ci : group_code_ints_) {
							asset_states_[ci].factor_entry_writer->OnGlobalTime(factor_api_time_ms);
						}
					}
#endif
				}
			}

			cpu_trace_ctx_.OnLoopIterationEnd();

			if (stop_flag_.load(std::memory_order_relaxed) && data_queue_->peek() == nullptr) {
				break;
			}
		}
		cpu_trace_ctx_.OnThreadExit();
		is_stopped_.store(true, std::memory_order_release);
	}

	// --- 线程标识与因子集配置 ---
	int thread_id_;
	size_t off_set_;
	std::vector<std::string> code_list_;
	std::vector<std::string> factor_entry_names_;
	factors::comm::FactorEntryConfig factor_entry_config_;
	factors::FactorSetColumnLayout factor_set_column_layout_;  // 与引擎一致的全局列布局（含截面集的 start，本线程只写 TS 子集）
	std::unordered_map<std::string, std::unordered_set<int>> factor_compute_time_points_map_;
	std::vector<int> send_time_points_vector_;
	std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map_;
	// --- 与引擎共享的队列与结果缓存 ---
	std::shared_ptr<moodycamel::ReaderWriterQueue<TickDataInfo>> data_queue_;
	std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>> result_queue_;
	std::shared_ptr<std::vector<std::vector<char>>> result_cache_;
	std::shared_ptr<std::vector<std::vector<unsigned char>>> readiness_cache_;

	// --- 单行 payload 尺寸（原始因子区 + SDP 包头） ---
	size_t row_factor_capacity_;  // 单行因子列数（= factor_size，供 RowWriter 写入边界检查）
	int single_asset_raw_data_size_;
	int single_asset_send_data_size_;

	// --- 资产槽位与线程控制 ---
	std::vector<AssetState> asset_states_;
	std::vector<int> group_code_ints_;
	std::atomic<bool> stop_flag_{false};
	std::atomic<bool> is_stopped_{false};
	std::atomic<pid_t> run_thread_os_tid_{0};
	std::atomic<int> run_thread_initial_os_cpu_{-1};
	std::atomic<int> run_thread_reallocate_os_cpu_{-1};
	std::thread run_thread_;

	const double scaler_;
	velatools::thread_cpu_trace::ThreadCpuTraceContext cpu_trace_ctx_;
	std::vector<std::vector<FactorComputeTimeStatsInfo>> factor_compute_time_stats_info_list_;
};
