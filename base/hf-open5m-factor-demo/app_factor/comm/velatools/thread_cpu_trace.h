#pragma once

/**
 * @file thread_cpu_trace.h
 * @brief 逻辑 CPU 轨迹采集与格式化（SYS_getcpu + TSC）。
 *
 * 轨迹阶段：initial -> reallocate -> run -> finish
 *
 * ## ThreadCpuTraceOptions
 * 采样与 settle 行为的参数包，由调用方构造并传入 ThreadCpuTraceContext。
 *
 * ## ThreadCpuTraceRecord
 * 保存四阶段 CPU 编号及 run 段列表；提供 FormatSummary / FormatTraceLog 等格式化接口。
 *
 * ## ThreadCpuTraceLoopSampler
 * 在主循环迭代末尾按「迭代次数」或「TSC 墙钟间隔」触发 run 阶段采样。
 *
 * ## ThreadCpuTraceContext
 * 对外入口：OnThreadEntry -> 循环内 OnLoopIterationEnd -> OnThreadExit。
 *
 * 使用示例：
 * @code
 * velatools::thread_cpu_trace::ThreadCpuTraceContext ctx(
 *     tsc_to_us,
 *     velatools::thread_cpu_trace::MakeTraceOptions(250, 100, 1000000.0, 25200));
 * ctx.OnThreadEntry(tid_out, initial_cpu_out, reallocate_cpu_out);
 * while (active) {
 *     // ... 主循环体 ...
 *     ctx.OnLoopIterationEnd();
 * }
 * ctx.OnThreadExit();
 * // ctx.record().FormatTraceLog(tid);
 * @endcode
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace velatools {
namespace thread_cpu_trace {

namespace detail {

inline uint64_t ReadTscCounter() {
#if defined(__has_builtin)
#if __has_builtin(__builtin_ia32_rdtsc)
	return __builtin_ia32_rdtsc();
#else
	unsigned int hi = 0;
	unsigned int lo = 0;
	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (static_cast<uint64_t>(hi) << 32) | lo;
#endif
#elif defined(__GNUC__) || defined(__clang__)
	unsigned int hi = 0;
	unsigned int lo = 0;
	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (static_cast<uint64_t>(hi) << 32) | lo;
#else
#error "thread_cpu_trace: ReadTscCounter unsupported on this platform"
#endif
}

}  // namespace detail

/** 轨迹采集参数；成员默认值在未显式传入时作为托底。 */
struct ThreadCpuTraceOptions {
	int realloc_settle_sleep_ms = 250;
	int loop_sample_interval = 100;
	double wall_sample_interval_us = 1000000.0;
	int run_segment_reserve = 25200;
};

/** 由标量参数构造 Options。 */
inline ThreadCpuTraceOptions MakeTraceOptions(int realloc_settle_sleep_ms, int loop_sample_interval,
    double wall_sample_interval_us, int run_segment_reserve) {
	ThreadCpuTraceOptions options;
	options.realloc_settle_sleep_ms = realloc_settle_sleep_ms;
	options.loop_sample_interval = loop_sample_interval;
	options.wall_sample_interval_us = wall_sample_interval_us;
	options.run_segment_reserve = run_segment_reserve;
	return options;
}

using CpuId = int;

/** 当前线程 sleep 指定毫秒。 */
inline void SleepForMs(int milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/** 读取当前线程所在逻辑 CPU；失败返回 -1。 */
inline CpuId ReadCurrentCpu() {
	unsigned cpu_u = 0;
	const long gcr = syscall(SYS_getcpu, &cpu_u, nullptr, nullptr);
	return (gcr == 0) ? static_cast<int>(cpu_u) : -1;
}

inline std::string FormatCpuToken(CpuId cpu) {
	return std::to_string(static_cast<long long>(cpu));
}

/** run 阶段：连续停留在同一逻辑 CPU 上的一段记录。 */
struct RunCpuSegment {
	CpuId cpu = -1;
	int duration_sec = 0;
	uint64_t start_tsc = 0;
	uint64_t end_tsc = 0;
};

inline void UpdateRunSegmentDuration(RunCpuSegment& seg, double tsc_to_us) {
	if (seg.end_tsc >= seg.start_tsc) {
		const double sec = static_cast<double>(seg.end_tsc - seg.start_tsc) * tsc_to_us / 1000000.0;
		seg.duration_sec = static_cast<int>(sec + 0.5);
	} else {
		seg.duration_sec = 0;
	}
}

inline std::string FormatDurationSecToken(int duration_sec) {
	return std::to_string(duration_sec) + "s";
}

/** 单线程完整 CPU 轨迹记录。 */
struct ThreadCpuTraceRecord {
	CpuId initial_cpu = -1;
	CpuId reallocate_cpu = -1;
	CpuId finish_cpu = -1;
	std::vector<RunCpuSegment> run_segments;

	void InitRunSegments(int run_segment_reserve) {
		run_segments.clear();
		run_segments.reserve(static_cast<size_t>(run_segment_reserve));
	}

	void SetInitial(CpuId cpu) { initial_cpu = cpu; }
	void SetAfterRealloc(CpuId cpu) { reallocate_cpu = cpu; }
	void SetFinish(CpuId cpu) { finish_cpu = cpu; }

	void AppendRunSample(CpuId cpu, uint64_t sample_tsc, double tsc_to_us, uint64_t interval_start_tsc) {
		if (!run_segments.empty() && run_segments.back().cpu == cpu) {
			RunCpuSegment& seg = run_segments.back();
			seg.end_tsc = sample_tsc;
			UpdateRunSegmentDuration(seg, tsc_to_us);
			return;
		}
		RunCpuSegment seg;
		seg.cpu = cpu;
		seg.start_tsc = interval_start_tsc;
		seg.end_tsc = sample_tsc;
		UpdateRunSegmentDuration(seg, tsc_to_us);
		run_segments.push_back(seg);
	}

	std::string FormatRunSegments() const {
		if (run_segments.empty()) {
			return "";
		}
		std::ostringstream oss;
		for (size_t i = 0; i < run_segments.size(); ++i) {
			if (i > 0) {
				oss << ',';
			}
			oss << run_segments[i].cpu << '(' << FormatDurationSecToken(run_segments[i].duration_sec) << ')';
		}
		return oss.str();
	}

	/** 四段摘要：initial -> reallocate -> run -> finish。 */
	std::string FormatSummary() const {
		return std::string("initial[") + FormatCpuToken(initial_cpu) + "] -> reallocate[" +
		       FormatCpuToken(reallocate_cpu) + "] -> run[" + FormatRunSegments() + "] -> finish[" +
		       FormatCpuToken(finish_cpu) + "]";
	}

	/** 带 tid 前缀的完整轨迹单行文本。 */
	std::string FormatTraceLog(pid_t tid) const {
		return std::string("Thread (tid = ") + std::to_string(static_cast<long long>(tid)) + "):  " + FormatSummary();
	}
};

/** 仅含 initial 与 reallocate 的行尾后缀（run/finish 尚未产生时）。 */
inline std::string FormatEntryPhaseSuffix(pid_t tid, CpuId initial_cpu, CpuId reallocate_cpu) {
	return std::string(" (tid = ") + std::to_string(static_cast<long long>(tid)) + "):  initial[" +
	       FormatCpuToken(initial_cpu) + "] -> reallocate[" + FormatCpuToken(reallocate_cpu) + "]";
}

/** 主循环底部采样器：按迭代次数或 TSC 间隔触发 run 采样。 */
class ThreadCpuTraceLoopSampler {
public:
	ThreadCpuTraceLoopSampler(double tsc_to_us, const ThreadCpuTraceOptions& options)
	    : tsc_to_us_(tsc_to_us),
	      loop_sample_interval_(options.loop_sample_interval),
	      wall_sample_interval_us_(options.wall_sample_interval_us) {}

	void ResetTimeBase() {
		last_sample_tsc_ = detail::ReadTscCounter();
		loop_count_ = 0;
	}

	void OnLoopIterationEnd(ThreadCpuTraceRecord& record) {
		++loop_count_;
		bool should_sample = false;
		if (loop_count_ % loop_sample_interval_ == 0) {
			should_sample = true;
		}
		const uint64_t now_tsc = detail::ReadTscCounter();
		if (last_sample_tsc_ == 0) {
			last_sample_tsc_ = now_tsc;
		} else if (static_cast<double>(now_tsc - last_sample_tsc_) * tsc_to_us_ >= wall_sample_interval_us_) {
			should_sample = true;
		}
		if (should_sample) {
			record.AppendRunSample(ReadCurrentCpu(), now_tsc, tsc_to_us_, last_sample_tsc_);
			last_sample_tsc_ = now_tsc;
		}
	}

private:
	double tsc_to_us_ = 0;
	int loop_sample_interval_ = 100;
	double wall_sample_interval_us_ = 1000000.0;
	uint64_t loop_count_ = 0;
	uint64_t last_sample_tsc_ = 0;
};

/** 轨迹采集上下文：串联入口、循环采样与退出三阶段。 */
class ThreadCpuTraceContext {
public:
	explicit ThreadCpuTraceContext(double tsc_to_us, const ThreadCpuTraceOptions& options = ThreadCpuTraceOptions())
	    : options_(options), loop_sampler_(tsc_to_us, options) {}

	const ThreadCpuTraceRecord& record() const { return record_; }

	void OnThreadEntry(std::atomic<pid_t>& tid_out, std::atomic<int>& initial_cpu_out,
	    std::atomic<int>& reallocate_cpu_out) {
		tid_out.store(static_cast<pid_t>(syscall(SYS_gettid)), std::memory_order_release);
		const CpuId cpu_at_entry = ReadCurrentCpu();
		initial_cpu_out.store(cpu_at_entry, std::memory_order_release);
		record_.SetInitial(cpu_at_entry);
		record_.InitRunSegments(options_.run_segment_reserve);
		SleepForMs(options_.realloc_settle_sleep_ms);
		const CpuId cpu_after_settle = ReadCurrentCpu();
		reallocate_cpu_out.store(cpu_after_settle, std::memory_order_release);
		record_.SetAfterRealloc(cpu_after_settle);
		loop_sampler_.ResetTimeBase();
	}

	void OnLoopIterationEnd() { loop_sampler_.OnLoopIterationEnd(record_); }

	void OnThreadExit() { record_.SetFinish(ReadCurrentCpu()); }

private:
	ThreadCpuTraceOptions options_;
	ThreadCpuTraceRecord record_;
	ThreadCpuTraceLoopSampler loop_sampler_;
};

}  // namespace thread_cpu_trace
}  // namespace velatools
