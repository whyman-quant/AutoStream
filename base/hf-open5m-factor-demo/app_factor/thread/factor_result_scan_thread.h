#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
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
#include "comm/velapex/spsc_queue.h"
#include "data/data_and_types.h"
#include "factors/_comm/core.h"
#include "sdp_handler/strategy_interface.h"

// 因子结果扫描线程：本地因子入口场景下收集各计算线程的完成通知并落盘，不连接模型引擎。
class FactorResultScanThread {
public:
	FactorResultScanThread()
	    : run_thread_(), scaler_(velapex::chrono_utils::RdtscTimer::GetScaler()), cpu_trace_ctx_(scaler_, velatools::thread_cpu_trace::MakeTraceOptions(constant_space::kThreadCpuTraceReallocSettleSleepMs, constant_space::kThreadCpuTraceLoopSampleInterval, constant_space::kThreadCpuTraceWallSampleIntervalUs, constant_space::kThreadCpuTraceRunSegmentReserve)) {}

	FactorResultScanThread(int queue_count, int factor_size, std::vector<std::string> asset_codes,
	    std::vector<int> send_time_points_vector,
	    std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map,
	    std::vector<std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>>> result_queues,
	    std::shared_ptr<std::vector<std::vector<char>>> result_cache,
	    std::shared_ptr<std::vector<std::vector<factors::fval_t>>> result_data)
	    : queue_count_(queue_count),
	      factor_size_(factor_size),
	      asset_codes_(std::move(asset_codes)),
	      send_time_points_vector_(std::move(send_time_points_vector)),
	      trigger_time_points_map_(std::move(trigger_time_points_map)),
	      result_queues_(std::move(result_queues)),
	      result_cache_(std::move(result_cache)),
	      result_data_(std::move(result_data)),
	      single_asset_raw_data_size_(factor_size * sizeof(factors::fval_t)),
	      single_asset_send_data_size_(sizeof(my_factor_double_v2) + single_asset_raw_data_size_),
	      collect_flags_(queue_count_, false),
	      counters_(send_time_points_vector_.size(), 0),
	      scaler_(velapex::chrono_utils::RdtscTimer::GetScaler()),
	      cpu_trace_ctx_(scaler_, velatools::thread_cpu_trace::MakeTraceOptions(constant_space::kThreadCpuTraceReallocSettleSleepMs, constant_space::kThreadCpuTraceLoopSampleInterval, constant_space::kThreadCpuTraceWallSampleIntervalUs, constant_space::kThreadCpuTraceRunSegmentReserve)) {
		WLOG("[FactorResultScanThread] FactorResultScanThread constructed successfully.");
	}

	~FactorResultScanThread() { Wait(); }

	void Start() { run_thread_ = std::move(std::thread(&FactorResultScanThread::ScanFunc, this)); }

	void Stop() noexcept { stop_flag_.store(true, std::memory_order_release); }

	void Wait() {
		if (run_thread_.joinable()) {
			run_thread_.join();
			WLOG(cpu_trace_ctx_.record().FormatTraceLog(GetThreadOsTid()));
		}
	}

	bool IsStopped() const noexcept { return is_stopped_.load(std::memory_order_acquire); }

	// 返回 run_thread_ 在 Linux 下的内核线程号（TID）；未启动或入口尚未执行时为 0。
	pid_t GetThreadOsTid() const noexcept {
		return run_thread_os_tid_.load(std::memory_order_acquire);
	}

	// 返回本线程刚进入 ScanFunc 时采样的逻辑 CPU 全局编号（仅入口写一次）。
	// -1：尚未进入线程体，或 syscall(SYS_getcpu) 失败；线程树中为 [os_cpu=-1]。
	// 之后若被调度迁核不会更新，本值始终为入口瞬间快照。
	int GetThreadInitialOsCpu() const noexcept {
		return run_thread_initial_os_cpu_.load(std::memory_order_acquire);
	}

	int GetThreadReAllocateOsCpu() const noexcept {
		return run_thread_reallocate_os_cpu_.load(std::memory_order_acquire);
	}

	std::vector<std::pair<int, std::string>> CollectRuntimeThreadTreeLines() const { return {}; }

private:
	void ScanFunc() {
		cpu_trace_ctx_.OnThreadEntry(run_thread_os_tid_, run_thread_initial_os_cpu_, run_thread_reallocate_os_cpu_);
		while (true) {
			CollectAndSend();
			if (stop_flag_.load(std::memory_order_relaxed)) {
				bool all_processed = true;
				for (int i = 0; i < queue_count_; i++) {
					if (!result_queues_[i]->Empty()) {
						all_processed = false;
						break;
					}
				}
				if (all_processed) {
					break;
				}
			}
			cpu_trace_ctx_.OnLoopIterationEnd();
		}
		cpu_trace_ctx_.OnThreadExit();
		is_stopped_.store(true, std::memory_order_release);
	}

	void CollectAndSend() {
		for (int i = 0; i < queue_count_; i++) {
			int send_time_batch_idx = -1;
			if (result_queues_[i]->Pop(send_time_batch_idx)) {
				counters_[send_time_batch_idx]++;
				if (counters_[send_time_batch_idx] == queue_count_) {
					SendData(send_time_batch_idx);
				}
			}
		}
	}

	void SendData(size_t send_time_point_idx) {
		int ts = send_time_points_vector_[send_time_point_idx];
		auto iter = trigger_time_points_map_->find(ts);
		if (iter == trigger_time_points_map_->end()) {
			WLOG(TO_STRING("[FactorResultScanThread] ERROR: Time stamp ", ts,
			    " not found in trigger_time_points_map_ during send"));
			return;
		}
		size_t valid_row_num = iter->second.valid_row_num;

		if (result_data_ != nullptr) {
			for (size_t i = 0; i < valid_row_num; i++) {
				std::vector<factors::fval_t> row_data(factor_size_);
				memcpy(row_data.data(),
				    result_cache_->at(send_time_point_idx).data() + i * single_asset_send_data_size_ +
				        sizeof(my_factor_double_v2),
				    single_asset_raw_data_size_);
				result_data_->emplace_back(std::move(row_data));
			}
			TriggerTimePointInfo& tpi = iter->second;
			tpi.saved_to_cache = true;
		}
		data_processed_counter_++;
		if (data_processed_counter_ <= 30 || data_processed_counter_ % 60 == 1) {
			WLOG(TO_STRING("[FactorResultScanThread] data processed counter:", data_processed_counter_,
			    "| trigger_time:", ts, "| data size:", valid_row_num, "x", factor_size_));
		}
	}

	int queue_count_;
	int factor_size_;
	std::vector<std::string> asset_codes_;
	std::vector<int> send_time_points_vector_;
	std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map_;
	std::vector<std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>>> result_queues_;
	std::shared_ptr<std::vector<std::vector<char>>> result_cache_;
	std::shared_ptr<std::vector<std::vector<factors::fval_t>>> result_data_;

	int single_asset_raw_data_size_;
	int single_asset_send_data_size_;

	std::atomic<bool> stop_flag_{false};
	std::atomic<bool> is_stopped_{false};
	std::atomic<pid_t> run_thread_os_tid_{0};
	std::atomic<int> run_thread_initial_os_cpu_{-1};
	std::atomic<int> run_thread_reallocate_os_cpu_{-1};
	std::thread run_thread_;
	std::vector<bool> collect_flags_;
	std::vector<int> counters_;
	int data_processed_counter_ = 0;

	const double scaler_;
	velatools::thread_cpu_trace::ThreadCpuTraceContext cpu_trace_ctx_;
};
