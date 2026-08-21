// factor_calculation_engine.cc
#include "factor_calculation_engine.h"

#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "comm/hdf5_utils.h"
#include "comm/print.hpp"
#include "comm/velatools/thread_cpu_trace.h"
#include "comm/tools.h"
#include "comm/velapex/chrono_utils.h"
#include "comm/velapex/memory_utils.h"
#include "comm/velapex/spmc_broadcast_buffer_recyclable.h"
#include "comm/velapex/spsc_queue.h"
#include "comm/velatools/datetime_utils.h"
#include "comm/velatools/table_utils.h"
#include "config/config_parser.h"
#include "data/constant_space.h"
#include "data/runtime_policy.h"
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_time_stats.h"

// 拉入各因子 factor_entry.h 中的 REGISTER_*，使注册逻辑进入本 TU，避免链静态因子库时 .o 被链接器丢弃。
#include "factors_include.h"

// 拉入 sdp_handler 相关头文件
#include "sdp_handler/core/sdp_handler.h"
#include "sdp_handler/quote_format_define.h"
#include "sdp_handler/strategy_interface.h"
#include "sdp_handler/utils/json.hpp"

using json = nlohmann::json;

using constant_space::kEndTimeHHMMSSms;
using constant_space::kStartTimeHHMMSSms;
using constant_space::kTimeIntervalMs;

using constant_space::kGlobalEndTimeHHMMSSms;
using constant_space::kGlobalStartTimeHHMMSSms;
using constant_space::kGlobalTimeIntervalMs;

using constant_space::kOrderNumPerAsset;
using constant_space::kQuoteNumPerAsset;
using constant_space::kTransNumPerAsset;

namespace {

// 生成线程树后缀：(tid = …):  initial[…] -> reallocate[…]
std::string FormatThreadOsTidCpuSuffix(pid_t tid, int initial_cpu, int realloc_cpu) {
	return velatools::thread_cpu_trace::FormatEntryPhaseSuffix(tid, initial_cpu, realloc_cpu);
}

// 构造并返回因子配置结构体
factors::comm::FactorEntryConfig GetFactorEntryConfig(const std::string& date, const std::string& ev_path,
    int omp_num_threads, const std::vector<std::string>& asset_codes = {}) {
	factors::comm::FactorEntryConfig config;
	config.date = date;
	config.omp_num_threads = omp_num_threads;
	// 新版本ev_path直接赋值，因子内部会访问对应文件夹
	config.ev_path = ev_path;
	// 股票代码列表，仅当因子为截面因子时有效
	config.asset_codes = asset_codes;
	// 兼容旧版本，保留ev_paths字段，但不推荐使用
	config.ev_paths["mengmai"] = ev_path + "/" + date + "/mengmai";
	config.ev_paths["chensi"] = ev_path + "/" + date + "/chensi";
	config.ev_paths["tangan"] = ev_path + "/" + date + "/tangan";
	return config;
}

// 时间统计汇总表：顶端 frozen 行不参与截断；数据行仅当日志行数过多时按 head/tail 保留。
constexpr size_t kTimeStatsFrozenPrefixRows = 1;
constexpr size_t kTimeStatsHeadDataRows = 25;
constexpr size_t kTimeStatsTailDataRows = 25;

}  // namespace

void FactorCalculationEngine::Init(
    int date, const std::vector<std::string>& codes, int thread_num, const config::ConfigData& config) {
	trade_date_str_ = std::to_string(date);
	WLOG(TO_STRING("[FactorCalculationEngine]", "trade date:", trade_date_str_));

	// --- 初始化解析 ---
	// 初始化资产代码
	InitAssetCodes(codes);
	// 解析配置信息，获取因子信息、保存信息、时间戳信息
	InitConfig(config);

	// --- 执行因子的静态初始化 ---
	// 时序因子，空 asset_codes
	auto ts_factor_config =
	    GetFactorEntryConfig(trade_date_str_, config.factor_ev.folder_path, config.factor_omp_num_threads, {});
	// 截面因子，传入所有股票代码
	auto cs_factor_config = GetFactorEntryConfig(
	    trade_date_str_, config.factor_ev.folder_path, config.factor_omp_num_threads, asset_codes_);
	// 对时序因子进行静态初始化
	if (!ts_factor_entry_names_.empty()) {
		factors::comm::FactorEntryRegistry::GetInstance().RunStaticInitializers(
		    ts_factor_entry_names_, ts_factor_config);
		WLOG(TO_STRING("[FactorCalculationEngine] Time-series factors static initialization finished. Count:",
		    ts_factor_entry_names_.size()));
	}
	// 对截面因子进行静态初始化
	if (!cs_factor_entry_names_.empty()) {
		factors::comm::FactorEntryRegistry::GetInstance().RunStaticInitializers(
		    cs_factor_entry_names_, cs_factor_config);
		WLOG(TO_STRING("[FactorCalculationEngine] Cross-sectional factors static initialization finished. Count:",
		    cs_factor_entry_names_.size()));
	}
	WLOG("[FactorCalculationEngine] All factors static initialization finished.");

	// --- 执行线程分配 ---
	// 确定线程分配方案
	AssignWorkLoads(thread_num);
	// 确定线程映射关系
	AssignThreadMapping();

	// --- 创建数据容器 ---

	// 创建单进单出的环形队列作为输入数据的缓存容器（时序因子使用）
	if (ts_calc_thread_num_ > 0) {
		for (int i = 0; i < asset_group_num_; i++) {
			data_queues_.emplace_back(std::make_shared<moodycamel::ReaderWriterQueue<TickDataInfo>>(1048576));
			WLOG(TO_STRING("[FactorCalculationEngine] Quote data buffer for asset group #", i, "created."));
		}
	}

	// 创建SPMCBuffer（截面因子使用）
	if (cs_calc_thread_num_ > 0) {
		size_t preallocate_size =
		    ((kQuoteNumPerAsset + kTransNumPerAsset + kOrderNumPerAsset) * (asset_codes_.size() + 10)) / 200;
		spmc_buffer_ = velapex::spmc_broadcast_buffer_recyclable::MakeAlignedSharedSpmc<TickDataInfo>(
		    cs_calc_thread_num_, preallocate_size);
		WLOG(TO_STRING("[FactorCalculationEngine] SPMCBuffer created for ", cs_calc_thread_num_,
		    " cross-sectional factor threads."));
	}

	// 创建行情内存池，并预分配容量，减少运行时频繁分配释放。
	size_t quote_preallocate_size = (kQuoteNumPerAsset * (asset_codes_.size() + 10)) / 30;
	size_t trans_preallocate_size = (kTransNumPerAsset * (asset_codes_.size() + 10)) / 120;
	size_t order_preallocate_size = (kOrderNumPerAsset * (asset_codes_.size() + 10)) / 100;
	quote_memory_pool_.ReserveMemory(quote_preallocate_size);
	trans_memory_pool_.ReserveMemory(trans_preallocate_size);
	order_memory_pool_.ReserveMemory(order_preallocate_size);
	quote_memory_pool_.SetDebugName("FactorEngine_Quote");
	trans_memory_pool_.SetDebugName("FactorEngine_Trans");
	order_memory_pool_.SetDebugName("FactorEngine_Order");

	// 创建时序因子计算线程输出结果的队列容器
	for (int i = 0; i < ts_calc_thread_num_; i++) {
		ts_result_queues_.emplace_back(std::make_shared<velapex::spsc_queue::SPSCQueue<int>>(10000));
		WLOG(TO_STRING("[FactorCalculationEngine] Result queue for time-series calculation thread #", i, "created."));
	}

	// 创建截面因子计算线程输出结果的队列容器
	for (int i = 0; i < cs_calc_thread_num_; i++) {
		cs_result_queues_.emplace_back(std::make_shared<velapex::spsc_queue::SPSCQueue<int>>(10000));
		WLOG(TO_STRING(
		    "[FactorCalculationEngine] Result queue for cross-sectional calculation thread #", i, "created."));
	}

	// 创建最终结果保存的容器（零拷贝优化：预分配内存并初始化）
	size_t single_asset_send_data_size = sizeof(my_factor_double_v2) + factor_size_ * sizeof(factors::fval_t);
	WLOG(TO_STRING("[FactorCalculationEngine] Single send data size (bytes):", single_asset_send_data_size));
	result_cache_ = std::make_shared<std::vector<std::vector<char>>>();
	result_cache_->resize(send_time_points_vector_.size());
	// 分配所有需要的内存
	for (size_t i = 0; i < send_time_points_vector_.size(); i++) {
		// 统一为所有股票分配内存，即使某些时刻某些股票没有值，也预留空间
		result_cache_->at(i).resize(asset_codes_.size() * single_asset_send_data_size, 0);
		// 预热内存，避免第一次访问时，发生缺页中断，导致性能下降
		velapex::memory_utils::TouchMemory(result_cache_->at(i).data(), result_cache_->at(i).size());
	}
	size_t total_memory_mb =
	    asset_codes_.size() * send_time_points_vector_.size() * single_asset_send_data_size / 1024 / 1024;
	WLOG(TO_STRING("[FactorCalculationEngine] Pre-allocated factor result pool:", send_time_points_vector_.size(),
	    "time points ×", asset_codes_.size(), "assets ×", single_asset_send_data_size, "Bytes =", total_memory_mb,
	    "MB"));

	// 赋值初始化：提前将一些值设置好，避免每次计算和发送时，浪费时间进行赋值操作
	for (size_t i = 0; i < send_time_points_vector_.size(); ++i) {
		int ts = send_time_points_vector_[i];
		// 安全检查：确保时间戳在 trigger_time_points_map_ 中存在
		auto iter = trigger_time_points_map_->find(ts);
		if (iter == trigger_time_points_map_->end()) {
			WLOG(TO_STRING(
			    "[FactorCalculationEngine] ERROR: Time stamp ", ts, " not found in trigger_time_points_map_"));
			continue;
		}
		// 提前将一些值设置为默认值，避免计算过程中浪费时间进行赋值操作
		// 目前因子值不经过平台直接发送给模型，模型不需要下面这些值，所以其实下面的赋值是多余的，不过为了规范，还是设置一下
		for (size_t j = 0; j < asset_codes_.size(); ++j) {
			my_factor_double_v2* factor_data =
			    (my_factor_double_v2*)(result_cache_->at(i).data() + j * single_asset_send_data_size);
			// 1 表示最后一个资产，0 表示中间资产
			factor_data->flag = j == iter->second.valid_row_num - 1 ? 1 : 0;
			// 模型计算不需要此值，填0即可；较真的话也可以从 sdp_handler 或 st_config_t 中获取，不过目前没有必要
			factor_data->fid = 0;
			strncpy(factor_data->ticker, asset_codes_[j].c_str(), sizeof(factor_data->ticker) - 1);
			factor_data->date = date;
			factor_data->count = factor_size_;
		}
	}
	WLOG(TO_STRING("[FactorCalculationEngine] Pre-set factor result pool values finished."));

	// 创建结果保存的容器，用于方便地使用现有接口保存结果到H5文件中
	result_data_ = std::make_shared<std::vector<std::vector<factors::fval_t>>>();
	result_data_->reserve(send_time_points_vector_.size() * asset_codes_.size());
	WLOG("[FactorCalculationEngine] Result save pool created.");

	// --- 创建线程 ---

	// 初始化时序因子计算线程（传入全局列布局，RowWriter 在 kSend 时按 layout 散射写入本线程持有的时序列）
	for (int i = 0; i < ts_calc_thread_num_; i++) {
		ts_calc_threads_.emplace_back(std::unique_ptr<FactorCalculationThread>(
		    new FactorCalculationThread(i, factor_size_, asset_group_off_set_[i], codes_in_asset_group_[i],
		        ts_factor_entry_names_, ts_factor_config, factor_set_column_layout_, factor_compute_time_points_map_,
		        send_time_points_vector_, trigger_time_points_map_, data_queues_[i], ts_result_queues_[i],
		        result_cache_)));
		WLOG(TO_STRING("[FactorCalculationEngine] Time-series calculation thread created: thread id", i));
	}

	// 初始化截面因子计算线程（cs_factor_start_indices_[i] 为对应截面集在全局行内的列起始，非「时序总宽」前缀假说）
	for (size_t i = 0; i < cs_factor_entry_names_.size(); i++) {
		const auto& factor_name = cs_factor_entry_names_[i];
		cs_calc_threads_.emplace_back(std::unique_ptr<CrossSectionalFactorCalculationThread>(
		    new CrossSectionalFactorCalculationThread(i, factor_name, cs_factor_config, asset_codes_,
		        factor_compute_time_points_map_, send_time_points_vector_, trigger_time_points_map_, spmc_buffer_,
		        cs_result_queues_[i], result_cache_, factor_size_, cs_factor_start_indices_[i])));
		WLOG(TO_STRING("[FactorCalculationEngine] Cross-sectional calculation thread created: thread id", i,
		    " for factor", factor_name));
	}

	// 收集所有结果队列
	std::vector<std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>>> all_result_queues;
	all_result_queues.insert(all_result_queues.end(), ts_result_queues_.begin(), ts_result_queues_.end());
	all_result_queues.insert(all_result_queues.end(), cs_result_queues_.begin(), cs_result_queues_.end());

	// 初始化扫描线程
	factor_result_scan_thread_ = std::unique_ptr<FactorResultScanThread>(
	    new FactorResultScanThread(ts_calc_thread_num_ + cs_calc_thread_num_, factor_size_, asset_codes_,
	        send_time_points_vector_, trigger_time_points_map_, all_result_queues, result_cache_, result_data_));
	WLOG("[FactorCalculationEngine] Scan thread created.");
}

void FactorCalculationEngine::Start() {
	// 启动时序因子计算线程
	for (auto& thread : ts_calc_threads_) {
		thread->Start();
		WLOG(
		    TO_STRING("[FactorCalculationEngine] Time-series Calculation Thread #", thread->GetThreadId(), "started."));
	}
	// 启动截面因子计算线程
	for (auto& thread : cs_calc_threads_) {
		thread->Start();
		WLOG(TO_STRING(
		    "[FactorCalculationEngine] Cross-sectional Calculation Thread #", thread->GetThreadId(), "started."));
	}
	// 启动扫描线程
	factor_result_scan_thread_->Start();
	WLOG("[FactorCalculationEngine] Scan Thread started.");
	std::this_thread::sleep_for(std::chrono::milliseconds(constant_space::kThreadCpuTracePostStartSyncSleepMs));
	WLOG(TO_STRING("[FactorCalculationEngine] post-start sleep ",
	              constant_space::kThreadCpuTracePostStartSyncSleepMs, "ms done for cpu trace wait."));
}

std::vector<std::pair<int, std::string>> FactorCalculationEngine::CollectRuntimeThreadTreeLines() const {
	std::vector<std::pair<int, std::string>> lines;
	lines.reserve(ts_calc_threads_.size() + cs_calc_threads_.size() + 1);

	for (const auto& thread : ts_calc_threads_) {
		if (!thread) continue;
		lines.push_back(std::make_pair(0, "FactorCalculationThread # " + std::to_string(thread->GetThreadId()) + " - " +
		                                      thread->GetFactorSetNamesLabel() +
		                                      FormatThreadOsTidCpuSuffix(thread->GetThreadOsTid(),
		                                          thread->GetThreadInitialOsCpu(), thread->GetThreadReAllocateOsCpu())));
		auto child_lines = thread->CollectRuntimeThreadTreeLines();
		for (const auto& child_line : child_lines) {
			lines.push_back(std::make_pair(child_line.first + 1, child_line.second));
		}
	}

	for (const auto& thread : cs_calc_threads_) {
		if (!thread) continue;
		lines.push_back(
		    std::make_pair(0, "CrossSectionalFactorCalculationThread # " + std::to_string(thread->GetThreadId()) +
		                          " - " + thread->factor_entry_name() +
		                          FormatThreadOsTidCpuSuffix(thread->GetThreadOsTid(), thread->GetThreadInitialOsCpu(),
		                              thread->GetThreadReAllocateOsCpu())));
		auto child_lines = thread->CollectRuntimeThreadTreeLines();
		for (const auto& child_line : child_lines) {
			lines.push_back(std::make_pair(child_line.first + 1, child_line.second));
		}
	}

	if (factor_result_scan_thread_) {
		lines.push_back(std::make_pair(0,
			std::string("FactorResultScanThread") + FormatThreadOsTidCpuSuffix(
			    factor_result_scan_thread_->GetThreadOsTid(), factor_result_scan_thread_->GetThreadInitialOsCpu(),
			    factor_result_scan_thread_->GetThreadReAllocateOsCpu())));
		auto child_lines = factor_result_scan_thread_->CollectRuntimeThreadTreeLines();
		for (const auto& child_line : child_lines) {
			lines.push_back(std::make_pair(child_line.first + 1, child_line.second));
		}
	}
	return lines;
}

void FactorCalculationEngine::WarmUp() {
	// 预热关键的、即将要用到的少量内存，减少首次访问时的 page fault 和缓存未命中
	// 预热顺序：最热的数据结构最后预热，使其更可能在 L1 缓存中（LRU 策略）

	// 从后向前预热 result_cache_
	// 注意：使用 size_t 时不能直接写 i >= 0，因为 size_t 是无符号类型，--i 会变成 SIZE_MAX
	for (size_t i = send_time_points_vector_.size(); i > 0; --i) {
		velapex::memory_utils::TouchMemory(result_cache_->at(i - 1).data(), result_cache_->at(i - 1).size());
	}
	WLOG(TO_STRING("[FactorCalculationEngine] Warmed up result_cache_."));

	WLOG("[FactorCalculationEngine] Memory warm-up completed.");
}

void FactorCalculationEngine::Stop() {
	// 如果不在运行状态，则直接返回
	if (stop_status_ != 0) {
		return;
	}
	stop_status_ = 1;

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	WLOG("[FactorCalculationEngine] ========== Stopping Threads ==========");
	// 停止时序因子计算线程
	for (auto& thread : ts_calc_threads_) {
		thread->Stop();
	}
	// 停止截面因子计算线程
	for (auto& thread : cs_calc_threads_) {
		thread->Stop();
	}
	// 等待所有时序因子计算线程结束
	for (auto& thread : ts_calc_threads_) {
		thread->Wait();
		WLOG(TO_STRING(
		    "[FactorCalculationEngine] Time-series Calculation Thread #", thread->GetThreadId(), "finished."));
	}
	// 等待所有截面因子计算线程结束
	for (auto& thread : cs_calc_threads_) {
		thread->Wait();
		WLOG(TO_STRING(
		    "[FactorCalculationEngine] Cross-sectional Calculation Thread #", thread->GetThreadId(), "finished."));
	}

	// 停止扫描线程
	factor_result_scan_thread_->Stop();
	// 等待扫描线程结束
	factor_result_scan_thread_->Wait();
	WLOG("[FactorCalculationEngine] Scan Thread finished.");
	WLOG("[FactorCalculationEngine] ======================================");

	// 保存结果
	if (result_data_ != nullptr) {
		try {
			SaveResultsToH5();
		} catch (const std::exception& e) {
			WLOG(TO_STRING("[FactorCalculationEngine] Failed to save factors to h5 file with exception:", e.what()));
		}
	}

// 打印时间统计信息
#ifndef ENABLE_APP_LIVE
	// live模式在析构函数中打印统计信息，本地模式在Stop()中打印统计信息
	// 强制睡眠0.5s，防止打印内容被其他线程打断
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	PrintTimeStats();
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif  // ENABLE_APP_LIVE

	stop_status_ = 2;
}

void FactorCalculationEngine::PrintTimeStats() {
	// 打印所有计算线程的统计信息
	// 如果live模式下，在Stop()中打印统计信息，最好是延迟一会儿再打印统计信息，防止打印内容被其他线程打断
	// std::this_thread::sleep_for(std::chrono::milliseconds(500));
	// 如果在析构函数中打印统计信息，可以保证所有线程都已结束，避免打印内容被其他线程打断
	// live 模式下推荐在析构函数中打印统计信息，本地模式下推荐在Stop()中打印统计信息

	// 打印因子的具体性能统计
	std::vector<factors::comm::FactorEntryBase*> all_factor_entry_ptrs;
	for (auto& thread : ts_calc_threads_) {
		thread->CollectFactorEntryPtrs(all_factor_entry_ptrs);
	}
	for (auto& thread : cs_calc_threads_) {
		thread->CollectFactorEntryPtrs(all_factor_entry_ptrs);
	}
	if (!all_factor_entry_ptrs.empty()) {
		WLOG("[FactorCalculationEngine] ================ Performance Statistics ================");
		WLOG(factors::GetTimeStatsInfo(all_factor_entry_ptrs, save_factor_stats_, factor_stats_dir_));
		WLOG("[FactorCalculationEngine] ========================================================");
	}

	for (auto& thread : ts_calc_threads_) {
		const auto& factor_compute_time_stats_info_list = thread->GetFactorComputeTimeStats();
		for (size_t factor_set_idx = 0; factor_set_idx < factor_compute_time_stats_info_list.size(); ++factor_set_idx) {
			for (size_t compute_time_idx = 0;
			     compute_time_idx < factor_compute_time_stats_info_list[factor_set_idx].size(); ++compute_time_idx) {
				const auto& factor_compute_time_stats_info =
				    factor_compute_time_stats_info_list[factor_set_idx][compute_time_idx];
				const int ts = factor_compute_time_stats_info.trigger_time_ms;
				const std::string& factor_set_name = factor_compute_time_stats_info.factor_set_name;
				if (factor_compute_time_stats_info.end_timestamp_us >
				    trigger_time_points_map_->at(ts).factor_compute_time_stats_map[factor_set_name].end_timestamp_us) {
					trigger_time_points_map_->at(ts).factor_compute_time_stats_map[factor_set_name] =
					    factor_compute_time_stats_info;
				}
			}
		}
	}
	for (auto& thread : cs_calc_threads_) {
		const auto& factor_compute_time_stats_info_list = thread->GetFactorComputeTimeStats();
		for (size_t factor_set_idx = 0; factor_set_idx < factor_compute_time_stats_info_list.size(); ++factor_set_idx) {
			for (size_t compute_time_idx = 0;
			     compute_time_idx < factor_compute_time_stats_info_list[factor_set_idx].size(); ++compute_time_idx) {
				const auto& factor_compute_time_stats_info =
				    factor_compute_time_stats_info_list[factor_set_idx][compute_time_idx];
				const int ts = factor_compute_time_stats_info.trigger_time_ms;
				const std::string& factor_set_name = factor_compute_time_stats_info.factor_set_name;
				if (factor_compute_time_stats_info.end_timestamp_us >
				    trigger_time_points_map_->at(ts).factor_compute_time_stats_map[factor_set_name].end_timestamp_us) {
					trigger_time_points_map_->at(ts).factor_compute_time_stats_map[factor_set_name] =
					    factor_compute_time_stats_info;
				}
			}
		}
	}
	std::vector<std::vector<std::string>> time_stats_table;
	time_stats_table.reserve(factor_entry_names_.size() * all_time_points_vector_.size() + 1);
	time_stats_table.push_back(
	    {"TimePoint", "Type", "FactorSet", "TickWait", "Compute", "StartTimestamp", "EndTimestamp"});
	auto to_string_with_3dp = [](double val) -> std::string {
		std::ostringstream oss;
		oss.precision(3);
		oss << std::fixed << val;
		return oss.str();
	};

	for (int ts : all_time_points_vector_) {
		auto iter = trigger_time_points_map_->find(ts);
		if (iter != trigger_time_points_map_->end()) {
			TriggerTimePointInfo& tpi = iter->second;
			if (tpi.is_compute_point) {
				for (const auto& factor_set_name : factor_entry_names_) {
					auto fctsi_iter = tpi.factor_compute_time_stats_map.find(factor_set_name);
					if (fctsi_iter != tpi.factor_compute_time_stats_map.end()) {
						const auto& fctsi = fctsi_iter->second;
						time_stats_table.push_back({std::to_string(ts), "Compute", factor_set_name,
						    to_string_with_3dp(fctsi.tick_wait_duration_us),
						    to_string_with_3dp(fctsi.compute_duration_us),
						    velatools::datetime_utils::GetTimeUsStrFromTimestampUs(
						        static_cast<long>(fctsi.start_timestamp_us)),
						    velatools::datetime_utils::GetTimeUsStrFromTimestampUs(
						        static_cast<long>(fctsi.end_timestamp_us))});
					}
				}
			}
		}
	}

	// 打印全流程时间统计汇总表（与 ModelCalculationEngine::PrintTimeStats 分支结构对齐）
	if (time_stats_table.size() > 1) {
		const std::vector<std::vector<std::string>> time_stats_table_for_log =
		    velatools::table_utils::TruncateTimeStatsTableForLog(time_stats_table, kTimeStatsFrozenPrefixRows,
		        kTimeStatsHeadDataRows, kTimeStatsTailDataRows);
		if (time_stats_table_for_log.size() < time_stats_table.size()) {
			WLOG(TO_STRING("[FactorCalculationEngine] Time Statistics Summary 数据行共 ",
			    (time_stats_table.size() - kTimeStatsFrozenPrefixRows), " 行，日志中仅输出前 ", kTimeStatsHeadDataRows,
			    " 行与后 ", kTimeStatsTailDataRows, " 行，中间已省略。"));
		}
		WLOG("[FactorCalculationEngine] ================ Time Statistics Summary ================");
		WLOG("[FactorCalculationEngine] unit: us");
		WLOG(velatools::table_utils::GetPrintableTable(time_stats_table_for_log));
	} else {
		WLOG("[FactorCalculationEngine] ================ Time Statistics Summary ================");
	}

	if (save_factor_stats_ && time_stats_table.size() > 1) {
		std::string base = factor_stats_dir_;
		if (!base.empty() && base.back() != '/') {
			base += '/';
		}
		const std::string csv_path = base + "factor_full_pipeline_segment_time_us.csv";
		if (velatools::table_utils::SaveStringMatrixToCsv(time_stats_table, csv_path)) {
			WLOG(TO_STRING("[FactorCalculationEngine] 全流程分段计时矩阵已保存 CSV:", csv_path));
		} else {
			WERR(TO_STRING("[FactorCalculationEngine] 全流程分段计时矩阵 CSV 保存失败:", csv_path));
		}
	}

	WLOG("[FactorCalculationEngine] =========================================================");
}

void FactorCalculationEngine::OnQuote(Stock_Internal_Book* quote, start_time_t t) {
	// 处理时间戳数据
	OnTimer(quote->exch_time, t);
	// 对 quote 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
	__builtin_prefetch(quote, 0, 1);
	// 如果此时依然有未处理的时间戳，则给对应的数据缓存发送行情数据
	if (send_time_point_idx_ < send_time_points_vector_.size()) {
		const int code_int = std::atoi(quote->ticker);
		AssetInfo* asset_info = asset_info_map_.find(code_int);
		if (asset_info != nullptr) {
			int grp_idx = -1;
			int ts_consumer_count = 0;
			if (ts_calc_thread_num_ > 0) {
				grp_idx = asset_info->group_idx;
				if (grp_idx > -1) {
					ts_consumer_count = 1;
				}
			}
			const int cs_consumer_count = (cs_calc_thread_num_ > 0) ? cs_calc_thread_num_ : 0;
			const int total_consumer_count = ts_consumer_count + cs_consumer_count;
			if (total_consumer_count <= 0) {
				return;
			}

			QuoteMemoryData* quote_memory_data = quote_memory_pool_.Allocate(total_consumer_count);
			memcpy(quote_memory_data->data, quote, sizeof(Stock_Internal_Book));

			// 1. 写入时序因子的队列
			if (ts_consumer_count > 0) {
				TickDataInfo qdi = TickDataInfo();
				qdi.code_int = code_int;
				qdi.data_type = TickDataKind::kQuote;
				qdi.q1 = quote_memory_data;
				qdi.start_time = t;
				if (!data_queues_[grp_idx]->enqueue(qdi)) {
					WLOG(TO_STRING("[FactorCalculationEngine] ERROR: Failed to enqueue quote data to queue ", grp_idx));
					quote_memory_data->Release(1);
				}
			}

			// 2. 写入截面因子的 SPMCBuffer（GetWriteSlot + CommitWrite 就地填充，避免栈上 TickDataInfo 再整结构拷贝）
			if (cs_consumer_count > 0) {
				TickDataInfo& slot = spmc_buffer_->GetWriteSlot();
				slot.code_int = code_int;
				slot.data_type = TickDataKind::kQuote;
				slot.q1 = quote_memory_data;
				slot.start_time = t;
				spmc_buffer_->CommitWrite();
			}
		}
	} else {
		Stop();
	}
}

void FactorCalculationEngine::OnTrans(Stock_Transaction_Internal_Book_New* quote, start_time_t t) {
	// 处理时间戳数据
	OnTimer(quote->int_time, t);
	// 对 trans 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
	__builtin_prefetch(quote, 0, 1);
	// 如果此时依然有未处理的时间戳，则给对应的数据缓存发送行情数据
	if (send_time_point_idx_ < send_time_points_vector_.size()) {
		const int code_int = std::atoi(quote->symbol);
		AssetInfo* asset_info = asset_info_map_.find(code_int);
		if (asset_info != nullptr) {
			int grp_idx = -1;
			int ts_consumer_count = 0;
			if (ts_calc_thread_num_ > 0) {
				grp_idx = asset_info->group_idx;
				if (grp_idx > -1) {
					ts_consumer_count = 1;
				}
			}
			const int cs_consumer_count = (cs_calc_thread_num_ > 0) ? cs_calc_thread_num_ : 0;
			const int total_consumer_count = ts_consumer_count + cs_consumer_count;
			if (total_consumer_count <= 0) {
				return;
			}

			TransMemoryData* trans_memory_data = trans_memory_pool_.Allocate(total_consumer_count);
			memcpy(trans_memory_data->data, quote, sizeof(Stock_Transaction_Internal_Book_New));

			// 1. 写入时序因子的队列
			if (ts_consumer_count > 0) {
				TickDataInfo qdi = TickDataInfo();
				qdi.code_int = code_int;
				qdi.data_type = TickDataKind::kTrans;
				qdi.q2 = trans_memory_data;
				qdi.start_time = t;
				if (!data_queues_[grp_idx]->enqueue(qdi)) {
					WLOG(TO_STRING("[FactorCalculationEngine] ERROR: Failed to enqueue trans data to queue ", grp_idx));
					trans_memory_data->Release(1);
				}
			}

			// 2. 写入截面因子的 SPMCBuffer（就地填充槽位，避免整结构拷贝）
			if (cs_consumer_count > 0) {
				TickDataInfo& slot = spmc_buffer_->GetWriteSlot();
				slot.code_int = code_int;
				slot.data_type = TickDataKind::kTrans;
				slot.q2 = trans_memory_data;
				slot.start_time = t;
				spmc_buffer_->CommitWrite();
			}
		}
	} else {
		Stop();
	}
}

void FactorCalculationEngine::OnOrder(Stock_Order_Internal_Book_New* quote, start_time_t t) {
	// 处理时间戳数据
	OnTimer(quote->int_time, t);
	// 对 order 只读预取： rw = 0 代表只读，locality = 1 表示短期有复用
	__builtin_prefetch(quote, 0, 1);
	// 如果此时依然有未处理的时间戳，则给对应的数据缓存发送行情数据
	if (send_time_point_idx_ < send_time_points_vector_.size()) {
		// 过滤掉 order_type 为 'S' 或 orderorino 为 0 的订单
		if (quote->order_type != 'S' && quote->orderorino != 0) {
			const int code_int = std::atoi(quote->symbol);
			AssetInfo* asset_info = asset_info_map_.find(code_int);
			if (asset_info != nullptr) {
				int grp_idx = -1;
				int ts_consumer_count = 0;
				if (ts_calc_thread_num_ > 0) {
					grp_idx = asset_info->group_idx;
					if (grp_idx > -1) {
						ts_consumer_count = 1;
					}
				}
				const int cs_consumer_count = (cs_calc_thread_num_ > 0) ? cs_calc_thread_num_ : 0;
				const int total_consumer_count = ts_consumer_count + cs_consumer_count;
				if (total_consumer_count <= 0) {
					return;
				}

				OrderMemoryData* order_memory_data = order_memory_pool_.Allocate(total_consumer_count);
				memcpy(order_memory_data->data, quote, sizeof(Stock_Order_Internal_Book_New));

				// 1. 写入时序因子的队列
				if (ts_consumer_count > 0) {
					TickDataInfo qdi = TickDataInfo();
					qdi.code_int = code_int;
					qdi.data_type = TickDataKind::kOrder;
					qdi.q3 = order_memory_data;
					qdi.start_time = t;
					if (!data_queues_[grp_idx]->enqueue(qdi)) {
						WLOG(TO_STRING(
						    "[FactorCalculationEngine] ERROR: Failed to enqueue order data to queue ", grp_idx));
						order_memory_data->Release(1);
					}
				}

				// 2. 写入截面因子的 SPMCBuffer（就地填充槽位，避免整结构拷贝）
				if (cs_consumer_count > 0) {
					TickDataInfo& slot = spmc_buffer_->GetWriteSlot();
					slot.code_int = code_int;
					slot.data_type = TickDataKind::kOrder;
					slot.q3 = order_memory_data;
					slot.start_time = t;
					spmc_buffer_->CommitWrite();
				}
			}
		}
	} else {
		Stop();
	}
}

// 处理时间戳数据，时间格式为HHMMSSmmm，比如092459970，表示09:24:59.970
void FactorCalculationEngine::OnTimer(int time_ms, start_time_t t) {
	// app_factor：无模型引擎，不调用 OnHeartbeatTimer。
	// 如果还有发送时间戳未处理
	if (send_time_point_idx_ < send_time_points_vector_.size()) {
		// 找出发送时间戳阈值（只有大于等于此阈值的发送时间戳才会被触发）
		int send_time_point_threshold = send_time_points_vector_[send_time_point_idx_];
		if (time_ms >= send_time_points_vector_[send_time_point_idx_]) {
			// 找到最近（最晚）的满足条件的发送时间戳：当前行情时间刚好大于等于的待处理的时间戳
			while ((send_time_point_idx_ + 1 < send_time_points_vector_.size()) &&
			       (send_time_points_vector_[send_time_point_idx_ + 1] <= time_ms)) {
				if (skip_missed_time_triggers_) {
					WLOG(TO_STRING("[FactorCalculationEngine] Skipped trigger time point",
					    send_time_points_vector_[send_time_point_idx_]));
				}
				send_time_point_idx_++;
			}
			// 如果设置了跳过延迟错过触发，则需要将阈值设为最近（最晚）的满足条件的发送时间戳
			if (skip_missed_time_triggers_) {
				send_time_point_threshold = send_time_points_vector_[send_time_point_idx_];
			}
			send_time_point_idx_++;
		}

		// 依次处理所有满足条件的时间点：只要当前行情时间大于等于下一个待处理的时间戳，就处理下一个时间点
		while (all_time_point_idx_ < all_time_points_vector_.size() &&
		       time_ms >= all_time_points_vector_[all_time_point_idx_]) {
			int time_ms_to_process = all_time_points_vector_[all_time_point_idx_];
			const int tp_data_time_ms = time_ms;
			const int tp_trigger_time_ms = time_ms_to_process;
			int time_operation = 0;
			int send_point_idx = -1;
			// 查找当前时间点是否为触发点（计算点或发送点），如果是则设置其开始处理时间
			auto iter = trigger_time_points_map_->find(time_ms_to_process);
			if (iter != trigger_time_points_map_->end()) {
				TriggerTimePointInfo& tpi = iter->second;
				// 记录该时间点被触发处理的精确开始时间
				tpi.set_start_time(t);
				if (tpi.is_compute_point) {
					time_operation |= runtime_policy::TimeOperationBits::kCompute;
				}
				if (tpi.is_send_point && time_ms_to_process >= send_time_point_threshold) {
					time_operation |= runtime_policy::TimeOperationBits::kSend;
					send_point_idx = tpi.send_point_idx;
				}
				if (tpi.call_OnGlobalTime) {
					time_operation |= runtime_policy::TimeOperationBits::kGlobalTime;
				}
			}

			// 1.
			// 向每个资产分组的数据队列发送一个时间戳类型的数据，通知时序因子计算线程进行对应时间点的处理
			if (ts_calc_thread_num_ > 0) {
				for (int i = 0; i < asset_group_num_; ++i) {
					TickDataInfo qdi = TickDataInfo();
					qdi.data_type = TickDataKind::kTimePoint;
					qdi.start_time = t;
					qdi.data_time_ms = tp_data_time_ms;
					qdi.trigger_time_ms = tp_trigger_time_ms;
					qdi.trigger_send_batch_idx = send_point_idx;
					qdi.time_operation = time_operation;
					// 队列会自动扩容，这里不会失败。但是为了规范，还是检查一下。
					if (!data_queues_[i]->enqueue(qdi)) {
						WLOG(
						    TO_STRING("[FactorCalculationEngine] ERROR: Failed to enqueue time "
						              "point data to queue",
						        i));
					}
				}
			}

			// 2. 向 SPMCBuffer 发送时间戳数据（消费端按 data_type 分支只读时间相关字段）
			if (cs_calc_thread_num_ > 0) {
				TickDataInfo& slot = spmc_buffer_->GetWriteSlot();
				slot.data_type = TickDataKind::kTimePoint;
				slot.start_time = t;
				slot.data_time_ms = tp_data_time_ms;
				slot.trigger_time_ms = tp_trigger_time_ms;
				slot.trigger_send_batch_idx = send_point_idx;
				slot.time_operation = time_operation;
				spmc_buffer_->CommitWrite();
			}

			all_time_point_idx_++;
		}
	} else {
		// 所有发送时间戳都已处理完毕，停止计算引擎
		Stop();
	}
}

void FactorCalculationEngine::InitAssetCodes(const std::vector<std::string>& codes) {
	// 筛选出深圳和上海的股票
	sz_asset_codes_.clear();
	sh_asset_codes_.clear();
	asset_codes_.clear();
	for (const auto& code : codes) {
		if (code.substr(0, 1) == "0" || code.substr(0, 1) == "3") {
			sz_asset_codes_.push_back(code.substr(0, 6));
		} else if (code.substr(0, 1) == "6") {
			sh_asset_codes_.push_back(code.substr(0, 6));
		}
	}
	// 前半部分存深圳股票，后半部分存上海股票
	asset_codes_.reserve(sz_asset_codes_.size() + sh_asset_codes_.size());
	asset_codes_.insert(asset_codes_.end(), sz_asset_codes_.begin(), sz_asset_codes_.end());
	asset_codes_.insert(asset_codes_.end(), sh_asset_codes_.begin(), sh_asset_codes_.end());
	WLOG(TO_STRING("[FactorCalculationEngine]", "sz asset codes size:", sz_asset_codes_.size()));
	WLOG(TO_STRING("[FactorCalculationEngine]", "sh asset codes size:", sh_asset_codes_.size()));
	WLOG(TO_STRING("[FactorCalculationEngine]", "asset codes size:", asset_codes_.size()));
}

void FactorCalculationEngine::InitConfig(const config::ConfigData& config) {
	/* 输出展示注册因子信息 */
	WLOG(factors::comm::GetRegisteredFactorsInfo());

	/* 获取详细的因子情况 */
	factor_entry_names_ = config.enabled_factor_set_names;
	auto& registry = factors::comm::FactorEntryRegistry::GetInstance();

	// 按配置顺序拆分因子集：时序与截面分别收集，组内保持 factors_config.factor_sets 中的相对先后。
	// enabled_factor_set_names 已由配置解析按 JSON 数组自上而下写入，此处仅做类型划分，不对时序因子重排。
	ts_factor_entry_names_.clear();
	cs_factor_entry_names_.clear();

	for (const auto& name : factor_entry_names_) {
		if (registry.IsCrossSectional(name)) {
			cs_factor_entry_names_.push_back(name);
		} else {
			ts_factor_entry_names_.push_back(name);
		}
	}

	// 计算因子数量和名称（列顺序与 factor_entry_names_ 配置顺序一致，支持时序/截面交错）
	factor_column_names_ = registry.GetStaticFactorNames(factor_entry_names_);
	factor_size_ = factor_column_names_.size();

	// 按 factors_config.factor_sets 自上而下累加列偏移，生成各因子集在 result_cache 行内的 {start, count}。
	// factor_column_names_、HDF5 列序、模型 input_t 列序均与此 layout 一致。
	factor_set_column_layout_.clear();
	size_t column_offset = 0;
	for (const auto& name : factor_entry_names_) {
		const size_t count = registry.GetStaticFactorSize({name});
		factor_set_column_layout_[name] = factors::FactorSetLayout{column_offset, count};
		column_offset += count;
	}

	/* 格式化输出因子信息 */
	WLOG("[FactorCalculationEngine] ================ Enabled Factor Summary ================");
	WLOG(TO_STRING("[FactorCalculationEngine] 时序因子集数量:", ts_factor_entry_names_.size(),
	    "| 截面因子集数量:", cs_factor_entry_names_.size(), "| 总因子数量:", factor_size_));
	for (size_t i = 0; i < factor_entry_names_.size(); i++) {
		auto factor_entry_name = factor_entry_names_[i];
		auto factor_names = registry.GetStaticFactorNames({factor_entry_name});
		bool is_cross_sectional = registry.IsCrossSectional(factor_entry_name);
		WLOG(TO_STRING("[FactorCalculationEngine] 因子集", i, ":", factor_entry_name,
		    "| 是否为截面因子:", is_cross_sectional ? "是" : "否"));
		WLOG(TO_STRING("[FactorCalculationEngine] ──── 因子个数:", factor_names.size()));
		WLOG(TO_STRING("[FactorCalculationEngine] ──── 因子列名:", factor_names));
	}
	WLOG("[FactorCalculationEngine] ========================================================");

	/* 获取时间戳信息 */

	// 汇总所有配置的计算和发送时间戳到 trigger_time_points_map_
	int max_send_time_hhmmssms = 0;
	int min_time_hhmmssms = 240000000;
	trigger_time_points_map_ = std::make_shared<std::unordered_map<int, TriggerTimePointInfo>>();

	// 处理发送点
	std::vector<int> send_time_list;
	if (!config.send_times.all_times.empty()) {
		send_time_list = config.send_times.all_times;
	} else {
		// 构造发送时间戳列表（同时也是计算时间戳）
		// 例如，开始时间是93000000，结束时间是93419000，间隔是1000毫秒，添加时间戳是{92700000}，跳过时间戳是{93000000}
		send_time_list = velatools::datetime_utils::GenerateUniformHHMMSSmsTimestampList(config.send_times.start,
		    config.send_times.end, config.send_times.interval, config.send_times.add, config.send_times.skip);
	}
	for (size_t send_point_idx = 0; send_point_idx < send_time_list.size(); send_point_idx++) {
		int ts = send_time_list[send_point_idx];
		// 注意：std::unordered_map 的 at() 方法在键不存在时会抛出异常，不会自动创建新元素。
		// 如果需要在键不存在时自动创建，可以使用 operator[]。
		TriggerTimePointInfo& tpi = (*trigger_time_points_map_)[ts];
		tpi.trigger_time_ms = ts;
		tpi.is_send_point = true;
		tpi.send_point_idx = send_point_idx;
		tpi.only_sz_available = ts <= 92500000 ? true : false;  // 如果时间戳小于等于 92500000，则认为此刻只有深圳有效
		tpi.valid_row_num = ts <= 92500000 ? sz_asset_codes_.size() : asset_codes_.size();
		max_send_time_hhmmssms = std::max(max_send_time_hhmmssms, ts);
		min_time_hhmmssms = std::min(min_time_hhmmssms, ts);
	}
	// 处理计算点
	factor_compute_time_points_map_.clear();
	for (const auto& factor_set : config.enabled_factor_sets) {
		std::vector<int> trigger_points;
		trigger_points.clear();
		if (factor_set.trigger_points.set) {
			if (!factor_set.trigger_points.all_times.empty()) {
				trigger_points = factor_set.trigger_points.all_times;
			} else {
				trigger_points = velatools::datetime_utils::GenerateUniformHHMMSSmsTimestampList(
				    factor_set.trigger_points.start, factor_set.trigger_points.end, factor_set.trigger_points.interval,
				    factor_set.trigger_points.add, factor_set.trigger_points.skip);
			}
		} else {
			trigger_points = send_time_list;
		}
		for (auto ts : trigger_points) {
			if (ts > max_send_time_hhmmssms) {
				WLOG(TO_STRING("[FactorCalculationEngine] 忽略不处理因子集", factor_set.name, "的计算时间戳", ts,
				    "，其值大于最大发送时间戳", max_send_time_hhmmssms));
				continue;
			}
			// 如果键不存在，[] 操作会自动创建时间戳对应的 TriggerTimePointInfo 对象
			TriggerTimePointInfo& tpi = (*trigger_time_points_map_)[ts];
			tpi.trigger_time_ms = ts;
			tpi.is_compute_point = true;
			tpi.only_sz_available =
			    ts <= 92500000 ? true : false;  // 如果时间戳小于等于 92500000，则认为此刻只有深圳有效
			tpi.valid_row_num = ts <= 92500000 ? sz_asset_codes_.size() : asset_codes_.size();
			tpi.factor_compute_time_stats_map[factor_set.name].trigger_time_ms = ts;
			tpi.factor_compute_time_stats_map[factor_set.name].factor_set_name = factor_set.name;
			factor_compute_time_points_map_[factor_set.name].insert(ts);
			min_time_hhmmssms = std::min(min_time_hhmmssms, ts);
		}
	}
	send_time_points_vector_.clear();
	send_time_points_vector_.reserve(trigger_time_points_map_->size());
	for (auto& kv : *trigger_time_points_map_) {
		if (kv.second.is_send_point) {
			send_time_points_vector_.push_back(kv.first);
		}
	}
	sort(send_time_points_vector_.begin(), send_time_points_vector_.end());

#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	/* 处理全局时间戳，用于触发因子调用函数OnGlobalTime */
	// NOTE: 目前全局时间戳仅用于调用因子的OnGlobalTime函数，目前只有open5m因子要求实现该函数接口
	// 例如，开始时间是93000000，结束时间是93501000，间隔是1000毫秒
	int max_global_end_time_hhmmssms = std::min(max_send_time_hhmmssms, kGlobalEndTimeHHMMSSms);
	global_time_points_vector_ = velatools::datetime_utils::GenerateUniformHHMMSSmsTimestampList(
	    kGlobalStartTimeHHMMSSms, max_global_end_time_hhmmssms, kGlobalTimeIntervalMs, {}, {});
	for (auto ts : global_time_points_vector_) {
		TriggerTimePointInfo& tpi = (*trigger_time_points_map_)[ts];
		tpi.trigger_time_ms = ts;
		tpi.call_OnGlobalTime = true;
	}
#else
	global_time_points_vector_.clear();
#endif

	// 将所有时间戳添加到all_time_points_vector_中
	all_time_points_vector_.clear();
	all_time_points_vector_.reserve(trigger_time_points_map_->size());
	for (auto& kv : *trigger_time_points_map_) {
		all_time_points_vector_.push_back(kv.first);
	}
	sort(all_time_points_vector_.begin(), all_time_points_vector_.end());

	skip_missed_time_triggers_ = config.skip_missed_time_triggers;

	/* 格式化输出时间戳信息 */
	WLOG("[FactorCalculationEngine] ================== Time Point Summary ==================");
	if (all_time_points_vector_.size() <= 100) {
		WLOG(TO_STRING("[FactorCalculationEngine] 时间戳数量:", trigger_time_points_map_->size()));
		size_t time_index = 0;
		for (int ts : all_time_points_vector_) {
			if (trigger_time_points_map_->find(ts) != trigger_time_points_map_->end()) {
				WLOG(TO_STRING("[FactorCalculationEngine] 时间戳", time_index, ":", ts));
				WLOG(TO_STRING("[FactorCalculationEngine] ──── 是否是计算点:",
				    trigger_time_points_map_->at(ts).is_compute_point ? "是" : "否",
				    "| 是否是发送点:", trigger_time_points_map_->at(ts).is_send_point ? "是" : "否",
				    "| 此刻只有深圳有效:", trigger_time_points_map_->at(ts).only_sz_available ? "是" : "否"));
				time_index++;
			}
		}
	}
	WLOG(TO_STRING(
	    "[FactorCalculationEngine] 发送时间戳:", send_time_points_vector_.size(), "|", send_time_points_vector_));
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	WLOG(TO_STRING("[FactorCalculationEngine] 调用OnGlobalTime函数的时间戳:", global_time_points_vector_.size(), "|",
	    global_time_points_vector_));
#endif
	WLOG(TO_STRING(
	    "[FactorCalculationEngine] 所有时间戳:", all_time_points_vector_.size(), "|", all_time_points_vector_));
	WLOG(TO_STRING("[FactorCalculationEngine] 是否跳过延迟错过触发:", skip_missed_time_triggers_ ? "是" : "否"));
	WLOG("[FactorCalculationEngine] ========================================================");

	/* 获取保存信息 */
	save_dir_ = config.save_info.factors_dir;
	save_file_path_ = config.save_info.factors_save_file;
	factor_data_dataset_name_ = config.save_info.factors_data_dataset_name;
	save_factor_stats_ = config.save_info.save_factor_stats;
	factor_stats_dir_ = config.save_info.factor_stats_dir;

	if (config.save_info.save_times.set) {
		if (!config.save_info.save_times.all_times.empty()) {
			save_times_ = config.save_info.save_times.all_times;
		} else {
			save_times_ =
			    velatools::datetime_utils::GenerateUniformHHMMSSmsTimestampList(config.save_info.save_times.start,
			        config.save_info.save_times.end, config.save_info.save_times.interval,
			        config.save_info.save_times.add, config.save_info.save_times.skip);
		}
	} else {
		save_times_ = send_time_points_vector_;
	}

	WLOG(TO_STRING("[FactorCalculationEngine] 因子保存目录:", save_dir_));
	if (save_by_split_timestamp_ == false) {
		WLOG(TO_STRING("[FactorCalculationEngine] 因子保存文件:", save_file_path_));
	}
	WLOG(TO_STRING("[FactorCalculationEngine] 分片文件因子数据集名:", factor_data_dataset_name_));
	WLOG(TO_STRING("[FactorCalculationEngine] 因子保存时间:", save_times_));
}

void FactorCalculationEngine::AssignWorkLoads(int allocable_thread_num) {
	// 对于截面因子，每个截面因子单独分配一个计算线程，负责该因子在所有股票上的计算
	cs_calc_thread_num_ = cs_factor_entry_names_.size();

	// 对于时序因子，将股票拆成若干组，每组一个计算线程，负责所有时序因子在该组股票上的计算
	// 如果没有时序因子，则不分配线程
	if (ts_factor_entry_names_.empty()) {
		ts_calc_thread_num_ = 0;
		asset_group_num_ = 0;
	} else {
		// 剩余线程分配给时序因子
		int remaining_threads = std::max(1, allocable_thread_num - cs_calc_thread_num_);
		ts_calc_thread_num_ = std::max(1, remaining_threads);
		const int code_count = static_cast<int>(asset_codes_.size());
		asset_group_num_ = code_count < ts_calc_thread_num_ ? code_count : ts_calc_thread_num_;
		ts_calc_thread_num_ = asset_group_num_;
	}

	/* 输出信息 */
	WLOG("[FactorCalculationEngine] ================ Thread Assignment Plan ================");
	WLOG(TO_STRING("[FactorCalculationEngine] 截面因子线程数量:", cs_calc_thread_num_,
	    "| 时序因子线程数量:", ts_calc_thread_num_));
	WLOG(TO_STRING("[FactorCalculationEngine] cpu_mhz =", velapex::chrono_utils::get_cpu_mhz()));
}

void FactorCalculationEngine::AssignThreadMapping() {
	/* 进行资产分组，记录资产与分组之间的双向映射关系
	 * 示例：
	 * 假设有 301 只票分成 3 组，因子分为 2 组，共 6 个线程的情况：
	 *
	 * | 资产分组  | 分组容量 |
	 * |----------|----------|
	 * | 0        | 101      |
	 * | 1        | 100      |
	 * | 2        | 100      |
	 */
	// 只有当有时序因子时才进行资产分组
	if (ts_calc_thread_num_ > 0) {
		int total_size = asset_codes_.size();                 // 总资产数
		int group_base_size = total_size / asset_group_num_;  // 每组基础大小
		int remainder = total_size % asset_group_num_;        // 余数(前remainder组的大小需+1)
		int threshold = (group_base_size + 1) * remainder;    // 前remainder组的边界索引

		asset_info_map_.clear();
		asset_group_off_set_.resize(asset_group_num_, 0);
		codes_in_asset_group_ = std::vector<std::vector<std::string>>();
		codes_in_asset_group_.resize(asset_group_num_);
		for (auto& asset_group : codes_in_asset_group_) {
			asset_group.reserve(group_base_size + 1);
		}
		/* 逐个扫描资产并按序分组，记录其在第几组的第几个 */

		// 资产的分组编号，表示其被分在了哪一组，用于后续在行情数据分发时快速定位，找到该把数据加入到哪一个行情缓存中。
		int group_index;
		for (int i = 0; i < total_size; i++) {
			if (i < threshold) {
				group_index = i / (group_base_size + 1);
			} else {
				group_index = (i - remainder) / group_base_size;
			}
			const int ci = std::atoi(asset_codes_[i].c_str());
			asset_info_map_[ci] = AssetInfo(ci, group_index);
			codes_in_asset_group_[group_index].push_back(asset_codes_[i]);
		}
		for (int i = 1; i < asset_group_num_; ++i) {
			asset_group_off_set_[i] = asset_group_off_set_[i - 1] + codes_in_asset_group_[i - 1].size();
		}

		// 输出信息
		for (int i = 0; i < asset_group_num_; i++) {
			WLOG(TO_STRING("[FactorCalculationEngine] 资产分组:", i, "| 分组容量:", codes_in_asset_group_[i].size()));
		}
	} else {
		// 没有时序因子时，仍建立 asset_info_map_（含 code_int，供行情路由）
		asset_info_map_.clear();
		for (size_t i = 0; i < asset_codes_.size(); i++) {
			const int ci = std::atoi(asset_codes_[i].c_str());
			asset_info_map_[ci] = AssetInfo(ci, -1);  // 无时序分组：group_idx 为 -1
		}
	}

	// 截面线程写入 result_cache 时的列起始下标：直接取 layout.start，不再假定时序因子占满行首前缀。
	cs_factor_start_indices_.clear();
	for (const auto& name : cs_factor_entry_names_) {
		const auto& layout = factor_set_column_layout_.at(name);
		cs_factor_start_indices_.push_back(layout.start);
		WLOG(TO_STRING("[FactorCalculationEngine] 截面因子", name, "起始位置:", layout.start,
		    "| 单股票因子数:", layout.count));
	}
}

void FactorCalculationEngine::SaveResultsToH5() {
	if (save_by_split_timestamp_) {
		SaveResultsToH5SplitTimestamp();
	} else {
		SaveResultsToH5CollectTimestamp();
	}
}

void FactorCalculationEngine::SaveResultsToH5CollectTimestamp() {
	// 创建保存目录
	tools_CreateDirRecursive(save_dir_.c_str());
	std::string temp_file_path = tools_GetTempFilePath(save_file_path_);
	if (tools_IsPathExist(temp_file_path.c_str())) {
		tools_DeleteFile(temp_file_path.c_str());
		if (!tools_IsPathExist(temp_file_path.c_str())) {
			WLOG(TO_STRING("[FactorCalculationEngine] Stale temp file removed before new save:", temp_file_path));
		} else {
			WERR(TO_STRING(
			    "[FactorCalculationEngine] Failed to remove stale temp file before new save:", temp_file_path));
		}
	}

	hid_t file_id = H5Fcreate(temp_file_path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (file_id < 0) {
		WERR(TO_STRING("[FactorCalculationEngine] Error: Failed to create temp h5 file:", temp_file_path));
		return;
	}

	std::unordered_set<int> save_time_points_set(save_times_.begin(), save_times_.end());
	size_t pos = 0;

	for (size_t i = 0; i < send_time_points_vector_.size(); ++i) {
		int ts = send_time_points_vector_[i];
		TriggerTimePointInfo& tpi = trigger_time_points_map_->at(ts);
		// 如果没有结果保存到了缓存中，则跳过
		if (!tpi.saved_to_cache) {
			continue;
		}
		// 如果不需要保存结果，则跳过
		if (save_time_points_set.find(ts) == save_time_points_set.end()) {
			pos += tpi.valid_row_num;
			continue;
		}

		// 保存资产代码列表
		if (tpi.only_sz_available) {
			hdf5_utils::Save1DStringVectorToH5(file_id, "codelist_" + std::to_string(ts), sz_asset_codes_);
		} else {
			hdf5_utils::Save1DStringVectorToH5(file_id, "codelist_" + std::to_string(ts), asset_codes_);
		}

		// 保存因子输出值
		std::vector<std::vector<factors::fval_t>> tmp_data;
		tmp_data.reserve(tpi.valid_row_num);
		for (size_t j = 0; j < tpi.valid_row_num; ++j) {
			tmp_data.push_back(result_data_->at(pos));
			pos++;
		}
		hdf5_utils::Save2DNumericVectorToH5(file_id, std::to_string(ts), tmp_data);
	}

	// 如果第一个时间戳和最后一个时间戳之间没有跨越92500000，则代表所有时刻的codelist相同，可以保存一个codelist
	if (send_time_points_vector_[0] <= 92500000 &&
	    send_time_points_vector_[send_time_points_vector_.size() - 1] <= 92500000) {
		hdf5_utils::Save1DStringVectorToH5(file_id, "codelist", sz_asset_codes_);
	} else if (send_time_points_vector_[0] > 92500000 &&
	           send_time_points_vector_[send_time_points_vector_.size() - 1] > 92500000) {
		hdf5_utils::Save1DStringVectorToH5(file_id, "codelist", asset_codes_);
	}

	// 保存因子输出列名
	hdf5_utils::Save1DStringVectorToH5(file_id, "factorlist", factor_column_names_);
	if (H5Fclose(file_id) < 0) {
		WERR(TO_STRING("[FactorCalculationEngine] Error: Failed to close temp h5 file:", temp_file_path));
		if (tools_IsPathExist(temp_file_path.c_str())) {
			tools_DeleteFile(temp_file_path.c_str());
			WLOG(TO_STRING("[FactorCalculationEngine] Temp file removed after close failure:", temp_file_path));
		}
		return;
	}
	if (!tools_CommitTempFile(save_file_path_, temp_file_path)) {
		WERR(TO_STRING(
		    "[FactorCalculationEngine] Error: Failed to commit temp h5 file:", temp_file_path, "->", save_file_path_));
		if (tools_IsPathExist(temp_file_path.c_str())) {
			tools_DeleteFile(temp_file_path.c_str());
			WLOG(TO_STRING("[FactorCalculationEngine] Temp file removed after commit failure:", temp_file_path));
		}
		return;
	}
	WLOG(TO_STRING("[FactorCalculationEngine] File saved:", save_file_path_));
}

void FactorCalculationEngine::SaveResultsToH5SplitTimestamp() {
	// 创建保存目录
	tools_CreateDirRecursive(save_dir_.c_str());

	std::unordered_set<int> save_time_points_set(save_times_.begin(), save_times_.end());
	size_t pos = 0;

	for (size_t i = 0; i < send_time_points_vector_.size(); ++i) {
		int ts = send_time_points_vector_[i];
		TriggerTimePointInfo& tpi = trigger_time_points_map_->at(ts);
		// 如果没有结果保存到了缓存中，则跳过
		if (!tpi.saved_to_cache) {
			continue;
		}
		// 如果不需要保存结果，则跳过
		if (save_time_points_set.find(ts) == save_time_points_set.end()) {
			pos += tpi.valid_row_num;
			continue;
		}
		// 保存文件路径
		// 例如，93005000 保存为 "093005.h5"
		std::string save_file_path =
		    save_dir_ + "/" + velatools::datetime_utils::GetHHMMSSStemFromTimestamp(ts) + ".h5";
		std::string temp_file_path = tools_GetTempFilePath(save_file_path);
		if (tools_IsPathExist(temp_file_path.c_str())) {
			tools_DeleteFile(temp_file_path.c_str());
			if (!tools_IsPathExist(temp_file_path.c_str())) {
				WLOG(TO_STRING("[FactorCalculationEngine] Stale temp file removed before new save:", temp_file_path));
			} else {
				WERR(TO_STRING(
				    "[FactorCalculationEngine] Failed to remove stale temp file before new save:", temp_file_path));
			}
		}
		hid_t file_id = H5Fcreate(temp_file_path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
		if (file_id < 0) {
			WERR(TO_STRING("[FactorCalculationEngine] Error: Failed to create temp h5 file:", temp_file_path));
			pos += tpi.valid_row_num;
			continue;
		}
		if (tpi.only_sz_available) {
			hdf5_utils::Save1DStringVectorToH5(file_id, "codelist", sz_asset_codes_);
		} else {
			hdf5_utils::Save1DStringVectorToH5(file_id, "codelist", asset_codes_);
		}
		// 保存因子输出值
		std::vector<std::vector<factors::fval_t>> tmp_data;
		tmp_data.reserve(tpi.valid_row_num);
		for (size_t j = 0; j < tpi.valid_row_num; ++j) {
			tmp_data.push_back(result_data_->at(pos));
			pos++;
		}
		hdf5_utils::Save2DNumericVectorToH5(file_id, factor_data_dataset_name_, tmp_data);
		// 保存因子输出列名
		hdf5_utils::Save1DStringVectorToH5(file_id, "factorlist", factor_column_names_);
		if (H5Fclose(file_id) < 0) {
			WERR(TO_STRING("[FactorCalculationEngine] Error: Failed to close temp h5 file:", temp_file_path));
			if (tools_IsPathExist(temp_file_path.c_str())) {
				tools_DeleteFile(temp_file_path.c_str());
				WLOG(TO_STRING("[FactorCalculationEngine] Temp file removed after close failure:", temp_file_path));
			}
			continue;
		}
		if (!tools_CommitTempFile(save_file_path, temp_file_path)) {
			WERR(TO_STRING("[FactorCalculationEngine] Error: Failed to commit temp h5 file:", temp_file_path, "->",
			    save_file_path));
			if (tools_IsPathExist(temp_file_path.c_str())) {
				tools_DeleteFile(temp_file_path.c_str());
				WLOG(TO_STRING("[FactorCalculationEngine] Temp file removed after commit failure:", temp_file_path));
			}
			continue;
		}
		WLOG(TO_STRING("[FactorCalculationEngine] File saved:", save_file_path));
	}
}
