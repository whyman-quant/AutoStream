#pragma once

#include <atomic>
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
#include "data/constant_space.h"
#include "comm/velapex/chrono_utils.h"
#include "comm/velapex/spmc_broadcast_buffer_recyclable.h"
#include "comm/velapex/spsc_queue.h"
#include "comm/velatools/datetime_utils.h"
#include "data/data_and_types.h"
#include "data/runtime_policy.h"
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_manager.h"
#include "factors/_comm/factor_entry_registry.h"
#include "sdp_handler/strategy_interface.h"

// 截面因子计算线程：作为 SPMC 缓冲区的消费者之一，从广播的 TickDataInfo 流中读取行情，
// 对全市场（或配置给定列表）计算单个截面因子集的输出。
// 多个截面线程共享同一 SPMCBroadcastBuffer，各自注册独立 consumer_token，并行消费同一批行情。
class CrossSectionalFactorCalculationThread {
public:
	// 默认构造函数
	CrossSectionalFactorCalculationThread() : run_thread_(), scaler_(0), cpu_trace_ctx_(0) {}

	// thread_id：线程编号；factor_entry_name：本线程负责的截面因子集名称；
	// factor_entry_config：含日期、EV、OMP 等；asset_codes：与引擎一致的代码表（全市场或子集）；
	// factor_compute_time_points_map / send_time_points_vector /
	// trigger_time_points_map：与时序线程相同的时间与触发语义；
	// spmc_buffer：共享广播缓冲；result_queue：计算完成后向扫描线程发 int 通知；
	// result_cache：按行的字节布局与时序线程共用；total_factor_size / cross_sectional_factor_start_idx：
	// 总因子列宽及本截面集在全局行内的列起始（= 引擎 factor_set_column_layout_[name].start，支持与时序交错）。
	CrossSectionalFactorCalculationThread(int thread_id, const std::string& entry_name,
	    factors::comm::FactorEntryConfig factor_entry_config, const std::vector<std::string>& asset_codes,
	    std::unordered_map<std::string, std::unordered_set<int>> factor_compute_time_points_map,
	    std::vector<int> send_time_points_vector,
	    std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map,
	    std::shared_ptr<velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<TickDataInfo>> spmc_buffer,
	    std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>> result_queue,
	    std::shared_ptr<std::vector<std::vector<char>>> result_cache, int total_factor_size,
	    size_t cross_sectional_factor_start_idx)
	    : thread_id_(thread_id),
	      factor_entry_name_(entry_name),
	      factor_entry_config_(std::move(factor_entry_config)),
	      asset_codes_(asset_codes),
	      factor_compute_time_points_map_(std::move(factor_compute_time_points_map)),
	      send_time_points_vector_(std::move(send_time_points_vector)),
	      trigger_time_points_map_(std::move(trigger_time_points_map)),
	      spmc_buffer_(std::move(spmc_buffer)),
	      result_queue_(std::move(result_queue)),
	      result_cache_(std::move(result_cache)),
	      total_factor_size_(total_factor_size),
	      cross_sectional_factor_start_idx_(cross_sectional_factor_start_idx),
	      scaler_(velapex::chrono_utils::RdtscTimer::GetScaler()),
	      cpu_trace_ctx_(scaler_, velatools::thread_cpu_trace::MakeTraceOptions(constant_space::kThreadCpuTraceReallocSettleSleepMs, constant_space::kThreadCpuTraceLoopSampleInterval, constant_space::kThreadCpuTraceWallSampleIntervalUs, constant_space::kThreadCpuTraceRunSegmentReserve)) {
		factor_compute_time_stats_info_list_.resize(1);
		{
			auto it = factor_compute_time_points_map_.find(factor_entry_name_);
			if (it != factor_compute_time_points_map_.end()) {
				factor_compute_time_stats_info_list_[0].reserve(it->second.size());
			}
		}

		// 注册SPMCBuffer消费者
		consumer_token_ = spmc_buffer_->RegisterConsumer();

		// 创建截面因子实例
		auto& registry = factors::comm::FactorEntryRegistry::GetInstance();
		factor_entry_ = registry.Create(factor_entry_name_, "888888", factor_entry_config_);
		if (!factor_entry_) {
			throw std::runtime_error("Failed to create cross-sectional factor: " + factor_entry_name_);
		}

		// 获取单个股票的因子数量
		single_asset_factor_size_ = factor_entry_->GetFactorSize();

		// 计算数据大小
		single_asset_raw_data_size_ = total_factor_size_ * sizeof(factors::fval_t);
		single_asset_send_data_size_ = sizeof(my_factor_double_v2) + single_asset_raw_data_size_;

		// 因子需要计算的时间戳集合，如果因子集的计算时间戳集合中包含当前时间戳，则触发计算
		compute_time_points_set_.clear();
		auto it = factor_compute_time_points_map_.find(factor_entry_name_);
		if (it != factor_compute_time_points_map_.end()) {
			compute_time_points_set_ = it->second;
		} else {
			for (const auto& time_point : send_time_points_vector_) {
				compute_time_points_set_.insert(time_point);
			}
		}

		WLOG(TO_STRING("[CrossSectionalFactorCalculationThread] Thread #", thread_id_, " for factor ",
		    factor_entry_name_, " constructed successfully."));
	}

	// 禁用拷贝构造函数
	CrossSectionalFactorCalculationThread(const CrossSectionalFactorCalculationThread&) = delete;

	// 禁用拷贝赋值函数
	CrossSectionalFactorCalculationThread& operator=(const CrossSectionalFactorCalculationThread&) = delete;

	// 析构函数
	~CrossSectionalFactorCalculationThread() { Wait(); }

	// 启动 CalcFunc 线程。
	void Start() { run_thread_ = std::move(std::thread(&CrossSectionalFactorCalculationThread::CalcFunc, this)); }

	// 请求停止线程（非阻塞）
	void Stop() noexcept { stop_flag_.store(true, std::memory_order_release); }

	// 等待线程结束（阻塞）
	void Wait() {
		if (run_thread_.joinable()) {
			run_thread_.join();
			WLOG(cpu_trace_ctx_.record().FormatTraceLog(GetThreadOsTid()));
		}
	}

	// 是否已退出主循环（非阻塞）。
	bool IsStopped() const noexcept { return is_stopped_.load(std::memory_order_acquire); }

	// 构造时传入的 thread_id。
	int GetThreadId() const { return thread_id_; }

	// 返回 run_thread_ 在 Linux 下的内核线程号（TID）；未启动或入口尚未执行时为 0。
	pid_t GetThreadOsTid() const noexcept {
		return run_thread_os_tid_.load(std::memory_order_acquire);
	}

	// 返回本线程刚进入 CalcFunc 时采样的逻辑 CPU 全局编号（仅入口写一次）。
	// -1：尚未进入线程体，或 syscall(SYS_getcpu) 失败；线程树中为 [os_cpu=-1]。
	// 之后若被调度迁核不会更新，本值始终为入口瞬间快照。
	int GetThreadInitialOsCpu() const noexcept {
		return run_thread_initial_os_cpu_.load(std::memory_order_acquire);
	}

	int GetThreadReAllocateOsCpu() const noexcept {
		return run_thread_reallocate_os_cpu_.load(std::memory_order_acquire);
	}

	const std::string& factor_entry_name() const { return factor_entry_name_; }

	// 将本线程唯一的截面 FactorEntry 指针装入 vector_fep，供引擎汇总统计。
	void CollectFactorEntryPtrs(std::vector<factors::comm::FactorEntryBase*>& vector_fep) const {
		if (factor_entry_) {
			vector_fep.push_back(factor_entry_.get());
		}
	}

	const std::vector<std::vector<FactorComputeTimeStatsInfo>>& GetFactorComputeTimeStats() const {
		return factor_compute_time_stats_info_list_;
	}

	// 返回该截面因子实例上报的额外线程（depth 相对本线程）。
	std::vector<std::pair<int, std::string>> CollectRuntimeThreadTreeLines() const {
		std::vector<std::pair<int, std::string>> lines;
		if (!factor_entry_) {
			return lines;
		}
		auto child_lines = factor_entry_->CollectRuntimeThreadTreeLines();
		for (const auto& child_line : child_lines) {
			lines.push_back(std::make_pair(child_line.first + 1, child_line.second));
		}
		return lines;
	}

private:
	// 因子计算线程的主逻辑函数
	void CalcFunc() {
		cpu_trace_ctx_.OnThreadEntry(run_thread_os_tid_, run_thread_initial_os_cpu_, run_thread_reallocate_os_cpu_);
		TickDataInfo q;

		while (true) {
			// 从SPMCBuffer读取数据（所有股票的数据）
			if (spmc_buffer_->TryRead(consumer_token_, q)) {
				if (q.data_type == TickDataKind::kQuote) {  // 如果是 Tick（Quote）数据
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q1, 0, 1);
					__builtin_prefetch(q.q1->data, 0, 1);
					factor_entry_->AddQuote(*q.q1->data);
					q.q1->Release(1);
				} else if (q.data_type == TickDataKind::kTrans) {  // 如果是 Trans 数据
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q2, 0, 1);
					__builtin_prefetch(q.q2->data, 0, 1);
					factor_entry_->AddTrans(*q.q2->data);
					q.q2->Release(1);
				} else if (q.data_type == TickDataKind::kOrder) {  // 如果是 Order 数据
					// 对池柄与 book 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
					__builtin_prefetch(q.q3, 0, 1);
					__builtin_prefetch(q.q3->data, 0, 1);
					factor_entry_->AddOrder(*q.q3->data);
					q.q3->Release(1);
				} else if (q.data_type == TickDataKind::kTimePoint) {  // 如果是时间戳数据
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
					const int factor_api_time_ms = q.data_time_ms;
#else
					const int factor_api_time_ms = q.trigger_time_ms;
#endif
					// 如果是计算时间戳，则触发计算
					if (q.time_operation & runtime_policy::TimeOperationBits::kCompute) {
						// 如果因子集的计算时间戳集合中包含当前时间戳
						if (compute_time_points_set_.find(q.trigger_time_ms) != compute_time_points_set_.end()) {
							const uint64_t now_tsc = velapex::chrono_utils::RdtscTimer()();
							const double compute_tick_wait_elapsed_us =
							    static_cast<double>(now_tsc - q.start_time.rdtsc_num) * scaler_;
							const uint64_t start_timestamp_us =
							    static_cast<uint64_t>(velatools::datetime_utils::NowTimestampUs());
							// kCompute：结果暂存于 factor_entry_ 内部，kSend 再按 cross_sectional_factor_start_idx_ 写入行
							factor_entry_->UpdateFactors(factor_api_time_ms);
							const uint64_t end_timestamp_us =
							    static_cast<uint64_t>(velatools::datetime_utils::NowTimestampUs());
							const uint64_t end_tsc = velapex::chrono_utils::RdtscTimer()();
							const double factor_calc_duration_us = static_cast<double>(end_tsc - now_tsc) * scaler_;
							factor_compute_time_stats_info_list_[0].emplace_back();
							auto& row = factor_compute_time_stats_info_list_[0].back();
							row.trigger_time_ms = q.trigger_time_ms;
							row.factor_set_name = factor_entry_name_;
							row.start_timestamp_us = start_timestamp_us;
							row.end_timestamp_us = end_timestamp_us;
							row.tick_wait_duration_us = compute_tick_wait_elapsed_us;
							row.compute_duration_us = factor_calc_duration_us;
						}
					}
					// 如果是发送时间戳，则更新并发送结果
					if (q.time_operation & runtime_policy::TimeOperationBits::kSend) {
						const auto& factor_data = factor_entry_->GetFactorValues();
						// factor_data包含所有股票的所有因子值，按股票顺序排列

						// 将结果按股票顺序写入到result_cache_的正确位置
						for (size_t asset_idx = 0; asset_idx < asset_codes_.size(); ++asset_idx) {
							// 计算该股票在result_cache中的基础位置
							char* base_dest = result_cache_->at(q.trigger_send_batch_idx).data() +
							                  asset_idx * single_asset_send_data_size_ + sizeof(my_factor_double_v2);

							// 计算该股票的因子值在factor_data中的位置
							const factors::fval_t* src = factor_data.data() + asset_idx * single_asset_factor_size_;

							// 写入本截面集在全局行内的列区间 [start, start+count)，不假定截面总在时序之后
							memcpy(base_dest + cross_sectional_factor_start_idx_ * sizeof(factors::fval_t), src,
							    single_asset_factor_size_ * sizeof(factors::fval_t));
						}

						// 通知扫描线程，该时间戳的结果已准备好
						while (!result_queue_->Push(q.trigger_send_batch_idx)) {
						}

#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
						// 调用AfterUpdateFactors函数（open5m独有接口）
						// NOTE: 此处也可以放在TriggerCompte后，但是为了节约时间不影响及时发送，所以放在这里
						// 对于open5m的因子，计算时间戳和发送时间戳是相同的，所以AfterUpdateFactors放在这里也能保证每次计算后会调用该函数
						factor_entry_->AfterUpdateFactors(factor_api_time_ms);
#endif
					}
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
					// 如果是调用OnGlobalTime函数，则调用OnGlobalTime函数
					if (q.time_operation & runtime_policy::TimeOperationBits::kGlobalTime) {
						factor_entry_->OnGlobalTime(factor_api_time_ms);
					}
#endif
				}
			}

			cpu_trace_ctx_.OnLoopIterationEnd();

			if (stop_flag_.load(std::memory_order_relaxed) && spmc_buffer_->IsConsumerFinished(consumer_token_)) {
				break;
			}
		}
		cpu_trace_ctx_.OnThreadExit();
		is_stopped_.store(true, std::memory_order_release);
	}

	// --- 线程标识与截面因子配置 ---
	int thread_id_;
	std::string factor_entry_name_;
	factors::comm::FactorEntryConfig factor_entry_config_;
	std::vector<std::string> asset_codes_;
	std::unordered_map<std::string, std::unordered_set<int>> factor_compute_time_points_map_;
	std::unordered_set<int> compute_time_points_set_;
	std::vector<int> send_time_points_vector_;
	std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map_;

	// --- 共享广播缓冲与本线程消费者 ---
	std::shared_ptr<velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<TickDataInfo>> spmc_buffer_;
	velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBufferConsumerToken<TickDataInfo> consumer_token_;

	std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>> result_queue_;
	std::shared_ptr<std::vector<std::vector<char>>> result_cache_;

	factors::comm::FactorEntryPtr factor_entry_;

	// --- 因子宽度与在全局因子行中的列偏移 ---
	int single_asset_factor_size_;
	int total_factor_size_;
	size_t cross_sectional_factor_start_idx_;  // 本截面集全局列起始（来自引擎 layout，非 ts 总宽累加）

	int single_asset_raw_data_size_;
	int single_asset_send_data_size_;

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
