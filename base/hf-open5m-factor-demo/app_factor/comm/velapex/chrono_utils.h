#pragma once

#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace velapex {
namespace chrono_utils {

// ============================================================================
// RDTSC 性能计时相关函数和类
// ============================================================================
// 使用 CPU 时间戳计数器（RDTSC）进行高精度、低开销的性能计时
// 适用于性能测试、基准测试等需要极高精度计时的场景

#if defined(__has_builtin)
#if __has_builtin(__builtin_ia32_rdtsc)
	// GCC/Clang 的内建函数（built-in），不用头文件，在 x86/x86_64 架构上直接可用
	// 这样写是合法的，可以用static inline函数替代#define。
	// 两者功能基本等价，但inline函数理论上会有极小的（几乎忽略不计）的调用开销，
	// 极端场景下（如数十亿次调用）#define版本略快，因为完全没有调用语义，但现代编译器能很好地对inline函数做优化，效率几乎一致。
	static inline uint64_t rdtsc() {
		return __builtin_ia32_rdtsc();
	}
	// 如果使用宏，会在全局展开并生效而不仅仅是namespace内，可能会影响其他代码，不推荐。
	// #define rdtsc() __builtin_ia32_rdtsc()
#else
	static inline uint64_t rdtsc() {
		unsigned int hi, lo;
		asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
		return ((uint64_t)hi << 32) | lo;
	}
#endif
#elif defined(__GNUC__) || defined(__clang__)
	static inline uint64_t rdtsc() {
		unsigned int hi, lo;
		asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
		return ((uint64_t)hi << 32) | lo;
	}
#else
	// 下面这个 #error 指令会在预处理（即编译前）阶段直接报错，导致编译过程终止。
	// 如果编译器走到这里说明未检测到支持的 rdtsc 实现，所以需要你手动实现 rdtsc 或根据平台适配。
	#error "No implementation for rdtsc on this platform"
#endif

// 通过 "定时采样" 的方式，在运行时动态测量当前 CPU 的实际主频（单位 MHz）
inline double get_cpu_mhz() {
	unsigned int eax, ebx, ecx, edx;
	uint64_t ts_start_us, ts_end_us, ts_temp_us, time_us, start_cycles, end_cycles;
	struct timeval ts;
	int has_invariant_tsc;
	double cpu_frequency_mhz;

	// 如果不支持 Invariant TSC（恒定时间戳计数器），打印警告提示测量结果可能不可靠
	asm volatile("cpuid"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		: "a"(0x80000007));
	has_invariant_tsc = edx & (1 << 8);
	if (!has_invariant_tsc)
		printf(
			"Warning: Cannot produce reliable results on machines without an "
			"invariant Time Stamp Counter\n");

	while (1) {
		gettimeofday(&ts, NULL);
		ts_temp_us = (ts.tv_usec + ts.tv_sec * 1000000);

		/* wait for microsecond to tick over to improve accuracy */
		do {
			gettimeofday(&ts, NULL);
			start_cycles = rdtsc();
			ts_start_us = (ts.tv_usec + ts.tv_sec * 1000000);
		} while (ts_start_us == ts_temp_us);

		/* protect against context switch between gettimeofday
		 * and timing_start by rechecking gettimeofday
		 */
		gettimeofday(&ts, NULL);
		ts_temp_us = (ts.tv_usec + ts.tv_sec * 1000000);
		if (ts_temp_us != ts_start_us) continue;

		break;
	}

	usleep(100000);

	// 校准时间（微秒），用于测量 CPU 主频
	static constexpr int kCalibrationTime = 1000;
	while (1) {
		gettimeofday(&ts, NULL);
		end_cycles = rdtsc();
		ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
		time_us = ts_end_us - ts_start_us;
		if (time_us < kCalibrationTime) continue;

		/* protect against context switch between gettimeofday
		 * and timing_end by rechecking gettimeofday
		 */
		gettimeofday(&ts, NULL);
		ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
		if (ts_end_us - ts_start_us > time_us) continue;

		break;
	}

	cpu_frequency_mhz = (double)(end_cycles - start_cycles) / (time_us);
	return cpu_frequency_mhz;
}

// 使用汇编获取时间戳计数器
struct RdtscTimer {
	uint64_t operator()() {
		// 等价于 __builtin_ia32_rdtsc()
		union {
			uint64_t tsc_64;
			struct TscParts {
				uint32_t lo_32;
				uint32_t hi_32;
			};
			TscParts parts;
		} tsc;
		asm volatile("rdtsc" : "=a"(tsc.parts.lo_32), "=d"(tsc.parts.hi_32));
		return tsc.tsc_64;
	}

	// 函数是static double 类型的，不会占用对象的内存空间，而是作为全局变量存在。
	static double GetScaler() {
		// 只有在第一次调用 GetScaler() 时会调用 get_cpu_mhz，
		// 具体是通过 static double scaler = [](){ ... }(); 静态局部变量实现的懒加载；
		// 以后再次调用 GetScaler() 时，scaler 已缓存，不会重复调用 get_cpu_mhz。
		static double scaler = []() {
			double cpu_hz = get_cpu_mhz();
			return cpu_hz > 0 ? 1.0 / cpu_hz : 1.0;
		}();
		return scaler;
	}
};

// 更可移植的高精度计时函数（纳秒）
struct HighResTimer {
	uint64_t operator()() {
		auto now = std::chrono::high_resolution_clock::now();
		return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	}

	static double GetScaler() { return 1.0; }
};

// 单调时钟计时器
struct SteadyClockTimer {
	uint64_t operator()() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

	static double GetScaler() { return 1.0; }
};

// 忙等待函数（使用 RDTSC 进行精确的忙等待）
// us: 等待时间（单位：微秒）
// 实现原理：使用 RDTSC（时间戳计数器）测量 CPU 周期数，通过忙等待达到指定时间
// 优势：相比 usleep，忙等待不会触发线程调度，保持 CPU 缓存热状态，延迟更低
// 适用场景：需要极低延迟的短时间等待（通常 < 1ms），如线程同步、初始化等待等
// 注意：长时间等待会占用 CPU 资源，不适合用于长时间休眠
inline void busy_wait_us(int us) {
	// 懒加载 CPU 主频（仅在第一次调用时测量，后续使用缓存值）
	// 性能优化：避免每次调用都测量 CPU 主频，减少开销
	static double cpu_mhz = []() {
		double mhz = get_cpu_mhz();
		return mhz > 0 ? mhz : 3000.0;  // 如果测量失败，使用默认值 3000 MHz
	}();

	uint64_t start_cycles = rdtsc();
	uint64_t target_cycles = static_cast<uint64_t>(us * cpu_mhz);

	// 忙等待循环：持续检查是否达到目标周期数
	while (rdtsc() - start_cycles < target_cycles) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
		// CPU pause 指令：减少功耗，降低 SMT 线程竞争，同时保持缓存热状态
		// 在 x86/x86_64 架构上，使用内联汇编或内建函数
		#if defined(__GNUC__) || defined(__clang__)
			asm volatile("pause");
		#endif
#endif
	}
}

// ============================================================================
// 计时统计结构体和类
// ============================================================================

// 计时统计结构体，线程不安全，不可跨线程使用
struct ElapsedTimeStats {
	uint64_t count = 0;
	double elapsed_sum = 0;
	double elapsed_max = 0;

	// 返回计数
	uint64_t GetCount() const { return count; }

	// 返回平均时间
	double GetElapsedAvg() const { return count > 0 ? elapsed_sum / count : 0.0; }

	// 返回最大时间
	double GetElapsedMax() const { return elapsed_max; }

	// 合并统计数据
	void Merge(const ElapsedTimeStats& other) {
		count += other.count;
		elapsed_sum += other.elapsed_sum;
		elapsed_max = std::max(elapsed_max, other.elapsed_max);
	}
};

// 对若干次独立测量的耗时样本（单位与 RdtscTimer::GetScaler() 一致，通常为 ns 纳秒）计算分布。
struct TimingDistribution {
	size_t count = 0;
	double min = 0;
	double max = 0;
	double mean = 0;
	double median = 0;
	double p25 = 0;
	double p75 = 0;
};

// 会原地排序 samples；空向量返回 count=0 其余为 0。
inline TimingDistribution ComputeTimingDistribution(std::vector<double> samples) {
	TimingDistribution d;
	d.count = samples.size();
	if (samples.empty()) {
		return d;
	}
	double sum = 0;
	for (double x : samples) {
		sum += x;
	}
	d.mean = sum / static_cast<double>(samples.size());
	std::sort(samples.begin(), samples.end());
	d.min = samples.front();
	d.max = samples.back();
	auto linear_pct = [&samples](double p) -> double {
		if (samples.size() == 1) {
			return samples[0];
		}
		const double pos = p * static_cast<double>(samples.size() - 1);
		const size_t lo = static_cast<size_t>(pos);
		const size_t hi = std::min(lo + 1, samples.size() - 1);
		const double frac = pos - static_cast<double>(lo);
		return samples[lo] * (1.0 - frac) + samples[hi] * frac;
	};
	d.p25 = linear_pct(0.25);
	d.median = linear_pct(0.50);
	d.p75 = linear_pct(0.75);
	return d;
}

template <typename T> class ScopedTiming {
public:
	explicit ScopedTiming(ElapsedTimeStats &time_stats) : time_stats_(time_stats) {
		scaler_ = T::GetScaler();
		start_ = T()();
	}
	~ScopedTiming() {
		const auto end = T()();
		const auto elapsed = (end - start_) * scaler_;
		time_stats_.count++;
		time_stats_.elapsed_sum += elapsed;
		if (elapsed > time_stats_.elapsed_max) {
			time_stats_.elapsed_max = elapsed;
		}
	}

private:
	ElapsedTimeStats &time_stats_;
	uint64_t start_;
	double scaler_;
};

} // namespace chrono_utils
} // namespace velapex
