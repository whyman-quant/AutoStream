#pragma once

#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace factors {
namespace timer {

#if defined(__has_builtin)
#if __has_builtin(__builtin_ia32_rdtsc)
// GCC/Clang 的内建函数（built-in），不用头文件，在 x86/x86_64 架构上直接可用
// 这样写是合法的，可以用static inline函数替代#define。
// 两者功能基本等价，但inline函数理论上会有极小的（几乎忽略不计）的调用开销，
// 极端场景下（如数十亿次调用）#define版本略快，因为完全没有调用语义，但现代编译器能很好地对inline函数做优化，效率几乎一致。
static inline uint64_t timer_rdtsc() { return __builtin_ia32_rdtsc(); }
// 如果使用宏，会在全局展开并生效而不仅仅是namespace内，可能会影响其他代码，不推荐。
// #define timer_rdtsc() __builtin_ia32_rdtsc()
#else
static inline uint64_t timer_rdtsc() {
	unsigned int hi, lo;
	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}
#endif
#elif defined(__GNUC__) || defined(__clang__)
static inline uint64_t timer_rdtsc() {
	unsigned int hi, lo;
	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}
#else
#error "No implementation for timer_rdtsc on this platform"
#endif

inline int shannon_get_cpufreq() {
	int cpu_hz = 0;
	char tmp[16] = {0};
	char cmd[] =
	    "cat /proc/cpuinfo |grep 'model name'|uniq|sed -e "
	    "'s/.*\\s\\+\\([.0-9]\\+\\)GHz.*/\\1/'";
	FILE* fp = popen(cmd, "r");
	if (fp) {
		fscanf(fp, "%s", tmp);

		cpu_hz = atof(tmp) * 1000;
		pclose(fp);
	}
	return cpu_hz;
}

// 通过 “定时采样” 的方式，在运行时动态测量当前 CPU 的实际主频（单位 MHz）
inline double get_cpu_mhz() {
	unsigned int eax, ebx, ecx, edx;
	uint64_t ts_start_us, ts_end_us, ts_temp_us, time_us, start_cycles, end_cycles;
	struct timeval ts;
	int has_invariant_tsc;
	double cpu_frequency_mhz;

	// 如果不支持 Invariant TSC（恒定时间戳计数器），打印警告提示测量结果可能不可靠
	asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000007));
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
			start_cycles = timer_rdtsc();
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
		end_cycles = timer_rdtsc();
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
struct RdtscTimer {  // wgh_shannon_rdtsc
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

struct CpuClockTimer {
	uint64_t operator()() { return clock(); }

	static double GetScaler() { return 1e6 / CLOCKS_PER_SEC; }
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

	// 返回总时间
	double GetElapsedSum() const { return elapsed_sum; }

	// 合并统计数据，适用于同类目下不同样例的合并统计，比如同一因子不同股票之间的合并统计
	void Merge(const ElapsedTimeStats& other) {
		count += other.count;
		elapsed_sum += other.elapsed_sum;
		elapsed_max = std::max(elapsed_max, other.elapsed_max);
	}

	// 汇总加和统计数据，适用于同级别不同类目下的加和统计，比如不同因子之间的加和统计
	void Summarize(const ElapsedTimeStats& other) {
		if (count > 0 && (count != other.count)) {
			std::cerr << "Count mismatch in Summarize:" << count << " != " << other.count << std::endl;
			count = std::max(count, other.count);
		}
		if (count == 0) {
			count = other.count;
		}
		elapsed_sum += other.elapsed_sum;
		elapsed_max += other.elapsed_max;
	}
};

template <typename T>
class ScopedTiming {
public:
	explicit ScopedTiming(ElapsedTimeStats& time_stats) : time_stats_(time_stats) {
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
	ElapsedTimeStats& time_stats_;
	uint64_t start_;
	double scaler_;
};

}  // namespace timer
}  // namespace factors