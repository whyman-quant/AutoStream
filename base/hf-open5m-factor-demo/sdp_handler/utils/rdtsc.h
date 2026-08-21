#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cmath>

#if defined(__has_builtin)
#if __has_builtin(__builtin_ia32_rdtsc)
	// GCC/Clang 的内建函数（built-in），不用头文件，在 x86/x86_64 架构上直接可用
	// 这样写是合法的，可以用static inline函数替代#define。
	// 两者功能基本等价，但inline函数理论上会有极小的（几乎忽略不计）的调用开销，
	// 极端场景下（如数十亿次调用）#define版本略快，因为完全没有调用语义，但现代编译器能很好地对inline函数做优化，效率几乎一致。
	static inline uint64_t sdp_rdtsc() {
		return __builtin_ia32_rdtsc();
	}
	// 如果使用宏，会在全局展开并生效而不仅仅是namespace内，可能会影响其他代码，不推荐。
	// #define sdp_rdtsc() __builtin_ia32_rdtsc()
#else
	static inline uint64_t sdp_rdtsc() {
		unsigned int hi, lo;
		asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
		return ((uint64_t)hi << 32) | lo;
	}
#endif
#elif defined(__GNUC__) || defined(__clang__)
	static inline uint64_t sdp_rdtsc() {
		unsigned int hi, lo;
		asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
		return ((uint64_t)hi << 32) | lo;
	}
#else
	// 下面这个 #error 指令会在预处理（即编译前）阶段直接报错，导致编译过程终止。
	// 如果编译器走到这里说明未检测到支持的 rdtsc 实现，所以需要你手动实现 sdp_rdtsc 或根据平台适配。
	#error "No implementation for sdp_rdtsc on this platform"
#endif

// 校准时间（微秒），用于测量 CPU 主频
// 使用 static constexpr 避免头文件被多次包含时的重复定义错误
static constexpr int kCalibrationTime = 1000;

inline double get_cpu_mhz()
{
    unsigned int eax, ebx, ecx, edx;
    uint64_t ts_start_us, ts_end_us, ts_temp_us, time_us, start_cycles, end_cycles;
    struct timeval ts;
    int has_invariant_tsc;
    double cpu_frequency_mhz;

    asm volatile("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (0x80000007));
    has_invariant_tsc = edx & (1 << 8);
    if (!has_invariant_tsc)
        printf("Warning: Cannot produce reliable results on machines without an invariant Time Stamp Counter\n");

    while (1) {
        gettimeofday(&ts, NULL);
        ts_temp_us = (ts.tv_usec + ts.tv_sec * 1000000);

        /* wait for microsecond to tick over to improve accuracy */
        do {
            gettimeofday(&ts, NULL);
            start_cycles = sdp_rdtsc();
            ts_start_us = (ts.tv_usec + ts.tv_sec * 1000000);
        } while (ts_start_us == ts_temp_us);

        /* protect against context switch between gettimeofday
         * and timing_start by rechecking gettimeofday
         */
        gettimeofday(&ts, NULL);
        ts_temp_us = (ts.tv_usec + ts.tv_sec * 1000000);
        if (ts_temp_us != ts_start_us)
            continue;

        break;
    }

    usleep(100000);

    while (1) {
        gettimeofday(&ts, NULL);
        end_cycles = sdp_rdtsc();
        ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
        time_us = ts_end_us - ts_start_us;
        if (time_us < kCalibrationTime)
            continue;

        /* protect against context switch between gettimeofday
         * and timing_end by rechecking gettimeofday
         */
        gettimeofday(&ts, NULL);
        ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
        if (ts_end_us - ts_start_us > time_us)
            continue;

        break;
    }

    cpu_frequency_mhz = (double)(end_cycles - start_cycles) / (time_us);
    return cpu_frequency_mhz;
}
