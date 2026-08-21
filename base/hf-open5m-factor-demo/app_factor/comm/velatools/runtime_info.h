#pragma once

/**
 * @file runtime_info.h
 * @brief 运行时环境信息获取模块（跨平台支持）
 *
 * @section 平台支持 Platform Support
 *
 * **完全支持：Linux**
 * - 所有功能完全可用
 * - 依赖：/proc 文件系统、/sys 文件系统、POSIX 命令（uname, ulimit, readelf, objdump 等）
 *
 * **部分支持：macOS**
 * - GetOSVersion()：可用（使用 sw_vers 或 uname）
 * - GetKernelVersion()：可用（使用 uname -r）
 * - GetSystemArchitecture()：可用（使用 uname -m）
 * - GetCPUModel()：可用（使用 sysctl machdep.cpu.brand_string）
 * - GetCPUCores()、GetCPUThreads()：可用（使用 sysctl hw.ncpu）
 * - GetCPUCurrentFreq()、GetCPUMaxFreq()：部分支持（使用 sysctl，可能不可用）
 * - GetCPUCacheInfo()：部分支持（使用 sysctl，L1/L2/L3 缓存）
 * - GetCPUFlags()：部分支持（使用 sysctl，仅检测 AVX/AVX2）
 * - GetMemoryInfo()：可用（使用 sysctl 和 mach API）
 * - GetAllSystemLimits()：可用（ulimit 命令）
 * - GetLibraryPath()：可用（环境变量，返回 LD_LIBRARY_PATH）
 *
 * **部分支持：Windows**
 * - GetOSVersion()：可用（使用 GetVersionEx）
 * - GetKernelVersion()：可用（使用 GetVersionEx）
 * - GetSystemArchitecture()：可用（使用 GetNativeSystemInfo）
 * - GetCPUModel()：可用（使用注册表）
 * - GetCPUCores()、GetCPUThreads()：可用（使用 GetSystemInfo）
 * - GetCPUCurrentFreq()、GetCPUMaxFreq()：不支持（需要 WMI，未实现）
 * - GetCPUCacheInfo()：不支持（需要 WMI/CPUID，未实现）
 * - GetCPUFlags()：不支持（需要 CPUID，未实现）
 * - GetMemoryInfo()：可用（使用 GlobalMemoryStatusEx）
 * - GetAllSystemLimits()：部分支持（使用 Windows API，返回简化信息）
 * - GetLibraryPath()：可用（环境变量，返回 PATH）
 *
 * **不支持：iOS/Android 等移动平台**
 * - 架构和系统调用方式完全不同，需要完全重写
 *
 * @section 主要依赖 Main Dependencies
 *
 * **Linux:**
 * - /proc/cpuinfo - CPU 信息
 * - /proc/meminfo - 内存信息
 * - /proc/self/exe - 可执行文件路径
 * - /sys/devices/system/cpu/ - CPU 频率和缓存信息
 * - uname, ulimit, readelf, objdump - POSIX 命令
 * - RDTSC 指令 - x86/x86_64 架构的 CPU 频率精确测量
 *
 * **macOS:**
 * - sysctl - 系统信息查询
 * - mach API - 内存信息
 * - uname, ulimit - POSIX 命令
 *
 * **Windows:**
 * - Windows API (GetVersionEx, GetSystemInfo, GlobalMemoryStatusEx 等)
 * - 注册表 API (RegOpenKeyEx, RegQueryValueEx)
 */

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <limits.h>
#include <unordered_map>
#include <iomanip>
#include <elf.h>

// 平台检测宏
#ifdef _WIN32
    #define RUNTIME_INFO_PLATFORM_WINDOWS
    #include <windows.h>
    #include <psapi.h>
    #include <intrin.h>
    #pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
    #define RUNTIME_INFO_PLATFORM_MACOS
    #include <sys/time.h>
    #include <unistd.h>
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <mach-o/dyld.h>  // For _NSGetExecutablePath
#elif defined(__linux__)
    #define RUNTIME_INFO_PLATFORM_LINUX
    #include <dlfcn.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <linux/limits.h>
#else
    #define RUNTIME_INFO_PLATFORM_UNKNOWN
    #include <sys/time.h>
    #include <unistd.h>
#endif

// RDTSC (Read Time-Stamp Counter) 支持
// 仅在 x86/x86_64 架构上可用
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // GCC/Clang 的内建函数（built-in），不用头文件，在 x86/x86_64 架构上直接可用
    #define RUNTIME_INFO_RDTSC() __builtin_ia32_rdtsc()
#else
    // 非 x86 架构，定义空宏（将回退到其他方法）
    #define RUNTIME_INFO_RDTSC() 0
#endif

namespace velatools {
namespace runtime_info {

// 辅助函数：读取文件内容
inline std::string ReadFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline std::string TrimWhitespace(const std::string& input) {
    if (input.empty()) {
        return "";
    }
    const std::string whitespace = " \t\n\r";
    size_t start = input.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = input.find_last_not_of(whitespace);
    return input.substr(start, end - start + 1);
}

template <typename Func>
inline std::string SafeStringCall(Func func, const std::string& fallback = "检测失败") {
    try {
        return func();
    } catch (...) {
        return fallback;
    }
}

template <typename Func>
inline std::vector<std::string> SafeVectorCall(Func func, const std::vector<std::string>& fallback = std::vector<std::string>{"检测失败"}) {
    try {
        return func();
    } catch (...) {
        return fallback;
    }
}

// 辅助函数：执行命令并获取输出
// 平台支持：Linux、macOS（使用 popen），Windows（使用 _popen）
inline std::string ExecuteCommand(const std::string& command) {
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        return "";
    }

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // 移除末尾的换行符
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

// 获取CPU型号（运行时）
// 平台支持：Linux（/proc/cpuinfo）、macOS（sysctl）、Windows（注册表/WMI）
inline std::string GetCPUModel() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::string cpuinfo = ReadFile("/proc/cpuinfo");
    if (cpuinfo.empty()) {
        return "unknown";
    }

    // 查找 "model name" 行
    std::istringstream iss(cpuinfo);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("model name") != std::string::npos) {
            // 提取冒号后的内容
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string model = line.substr(colon_pos + 1);
                // 去除前后空白
                size_t start = model.find_first_not_of(" \t");
                size_t end = model.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    return model.substr(start, end - start + 1);
                }
            }
            break;
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    size_t size = 0;
    sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0);
    if (size > 0) {
        std::vector<char> buffer(size);
        sysctlbyname("machdep.cpu.brand_string", buffer.data(), &size, nullptr, 0);
        return std::string(buffer.data());
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    // Windows: 使用注册表获取 CPU 型号
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256];
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buffer);
        }
        RegCloseKey(hKey);
    }
    return "unknown";
#else
    return "unknown";
#endif
}

// 获取内存信息（总内存和可用内存）
// 平台支持：Linux（/proc/meminfo）、macOS（sysctl）、Windows（GlobalMemoryStatusEx）
inline std::string GetMemoryInfo() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::string meminfo = ReadFile("/proc/meminfo");
    if (meminfo.empty()) {
        return "unknown";
    }

    std::istringstream iss(meminfo);
    std::string line;
    long long mem_total_kb = 0;
    long long mem_available_kb = 0;

    while (std::getline(iss, line)) {
        if (line.find("MemTotal:") == 0) {
            std::istringstream line_stream(line);
            std::string label, value, unit;
            line_stream >> label >> value >> unit;
            mem_total_kb = std::stoll(value);
        } else if (line.find("MemAvailable:") == 0) {
            std::istringstream line_stream(line);
            std::string label, value, unit;
            line_stream >> label >> value >> unit;
            mem_available_kb = std::stoll(value);
            break;
        }
    }

    if (mem_total_kb > 0) {
        double mem_total_gb = mem_total_kb / 1024.0 / 1024.0;
        double mem_available_gb = mem_available_kb / 1024.0 / 1024.0;
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << mem_total_gb << " GB (可用: " << mem_available_gb << " GB)";
        return oss.str();
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    uint64_t mem_total = 0;
    size_t size = sizeof(mem_total);
    if (sysctlbyname("hw.memsize", &mem_total, &size, nullptr, 0) == 0) {
        vm_size_t page_size;
        vm_statistics64_data_t vm_stat;
        mach_port_t mach_port = mach_host_self();
        mach_msg_type_number_t count = sizeof(vm_stat) / sizeof(natural_t);

        if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
            host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
            uint64_t mem_free = vm_stat.free_count * page_size;
            double mem_total_gb = mem_total / 1024.0 / 1024.0 / 1024.0;
            double mem_free_gb = mem_free / 1024.0 / 1024.0 / 1024.0;
            std::ostringstream oss;
            oss.precision(2);
            oss << std::fixed << mem_total_gb << " GB (可用: " << mem_free_gb << " GB)";
            return oss.str();
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        double mem_total_gb = memInfo.ullTotalPhys / 1024.0 / 1024.0 / 1024.0;
        double mem_available_gb = memInfo.ullAvailPhys / 1024.0 / 1024.0 / 1024.0;
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << mem_total_gb << " GB (可用: " << mem_available_gb << " GB)";
        return oss.str();
    }
    return "unknown";
#else
    return "unknown";
#endif
}

inline std::string GetTransparentHugePageStatus() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    const std::string enabled_path = "/sys/kernel/mm/transparent_hugepage/enabled";
    std::string enabled_raw = ReadFile(enabled_path);
    if (enabled_raw.empty()) {
        return "unknown";
    }
    std::string enabled = TrimWhitespace(enabled_raw);
    size_t newline_pos = enabled.find('\n');
    if (newline_pos != std::string::npos) {
        enabled = enabled.substr(0, newline_pos);
    }
    std::string current_mode = "unknown";
    size_t left = enabled.find('[');
    size_t right = enabled.find(']', left);
    if (left != std::string::npos && right != std::string::npos && right > left + 1) {
        current_mode = enabled.substr(left + 1, right - left - 1);
    } else {
        std::istringstream iss(enabled);
        iss >> current_mode;
        if (current_mode.empty()) {
            current_mode = "unknown";
        }
    }
    bool enabled_flag = (current_mode == "always" || current_mode == "madvise");
    std::ostringstream oss;
    oss << "当前模式: " << current_mode;
    if (enabled_flag) {
        oss << " (THP 可用)";
    } else {
        oss << " (THP 未启用)";
    }
    return oss.str();
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    return "macOS 不支持 THP";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    return "Windows 不支持 THP";
#else
    return "unknown";
#endif
}

// 获取 LD_LIBRARY_PATH 环境变量（Linux）或 PATH（Windows/macOS）
// 平台支持：跨平台（所有支持环境变量的系统）
inline std::string GetLibraryPath() {
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    const char* path = std::getenv("PATH");
    if (path != nullptr && std::strlen(path) > 0) {
        return std::string(path);
    }
    return "未知";
#else
    const char* ld_path = std::getenv("LD_LIBRARY_PATH");
    if (ld_path != nullptr && std::strlen(ld_path) > 0) {
        return std::string(ld_path);
    }
    return "未知";
#endif
}

// 获取操作系统版本
// 平台支持：Linux（/etc/os-release）、macOS（sysctl）、Windows（GetVersionEx）
inline std::string GetOSVersion() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    // 首先尝试读取 /etc/os-release
    std::string os_release = ReadFile("/etc/os-release");
    if (!os_release.empty()) {
        std::istringstream iss(os_release);
        std::string line;
        std::string name, version;
        while (std::getline(iss, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos) {
                    std::string value = line.substr(eq_pos + 1);
                    // 移除引号
                    if (value.front() == '"' && value.back() == '"') {
                        value = value.substr(1, value.length() - 2);
                    }
                    return value;
                }
            }
        }
    }

    // 如果失败，使用 uname -a
    std::string uname_result = ExecuteCommand("uname -a 2>/dev/null");
    if (!uname_result.empty()) {
        return uname_result;
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    // macOS: 使用 sw_vers 命令
    std::string result = ExecuteCommand("sw_vers -productName 2>/dev/null");
    std::string version = ExecuteCommand("sw_vers -productVersion 2>/dev/null");
    if (!result.empty() && !version.empty()) {
        return result + " " + version;
    }
    // 回退到 uname -a
    std::string uname_result = ExecuteCommand("uname -a 2>/dev/null");
    if (!uname_result.empty()) {
        return uname_result;
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        std::ostringstream oss;
        oss << "Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion;
        if (osvi.dwBuildNumber > 0) {
            oss << " Build " << osvi.dwBuildNumber;
        }
        return oss.str();
    }
    return "unknown";
#else
    // 回退到 uname -a
    std::string uname_result = ExecuteCommand("uname -a 2>/dev/null");
    if (!uname_result.empty()) {
        return uname_result;
    }
    return "unknown";
#endif
}

// 获取内核版本
// 平台支持：Linux（uname -r）、macOS（uname -r）、Windows（GetVersionEx）
inline std::string GetKernelVersion() {
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        std::ostringstream oss;
        oss << osvi.dwMajorVersion << "." << osvi.dwMinorVersion << "." << osvi.dwBuildNumber;
        return oss.str();
    }
    return "unknown";
#else
    std::string result = ExecuteCommand("uname -r 2>/dev/null");
    if (!result.empty()) {
        return result;
    }
    return "unknown";
#endif
}

// 获取系统架构
// 平台支持：Linux（uname -m）、macOS（uname -m）、Windows（GetNativeSystemInfo）
inline std::string GetSystemArchitecture() {
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "x86_64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return "x86";
        case PROCESSOR_ARCHITECTURE_ARM:
            return "ARM";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "ARM64";
        default:
            return "unknown";
    }
#else
    std::string result = ExecuteCommand("uname -m 2>/dev/null");
    if (!result.empty()) {
        return result;
    }
    return "unknown";
#endif
}

// 获取CPU核心数（逻辑核心数）
// 平台支持：Linux（/proc/cpuinfo）、macOS（sysctl）、Windows（GetSystemInfo）
inline std::string GetCPUCores() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::string cpuinfo = ReadFile("/proc/cpuinfo");
    if (cpuinfo.empty()) {
        return "unknown";
    }

    // 统计 "processor" 行的数量（逻辑核心数）
    std::istringstream iss(cpuinfo);
    std::string line;
    int processor_count = 0;
    while (std::getline(iss, line)) {
        if (line.find("processor") == 0) {
            processor_count++;
        }
    }

    if (processor_count > 0) {
        return std::to_string(processor_count);
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    int num_cores = 0;
    size_t size = sizeof(num_cores);
    if (sysctlbyname("hw.ncpu", &num_cores, &size, nullptr, 0) == 0) {
        return std::to_string(num_cores);
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return std::to_string(si.dwNumberOfProcessors);
#else
    return "unknown";
#endif
}

// 获取CPU线程数（逻辑核心数，通常等于processor数量）
inline std::string GetCPUThreads() {
    // 逻辑核心数通常等于 /proc/cpuinfo 中的 processor 数量
    return GetCPUCores();
}

// 检测是否支持恒定 TSC (Invariant TSC)
// 平台支持：Linux（CPUID）、macOS（CPUID）、Windows（CPUID，x86/x86_64 架构）
inline std::string GetTSCSupport() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // 使用 CPUID 指令检测恒定 TSC 支持
    // CPUID leaf 0x80000007, EDX bit 8 表示 Invariant TSC
    unsigned int eax, ebx, ecx, edx;

#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    // Windows: 使用 __cpuid 内建函数
    int cpuInfo[4];
    __cpuid(cpuInfo, 0x80000007);
    eax = cpuInfo[0];
    ebx = cpuInfo[1];
    ecx = cpuInfo[2];
    edx = cpuInfo[3];
#else
    // Linux/macOS: 使用内联汇编
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x80000007));
#endif

    bool has_invariant_tsc = (edx & (1 << 8)) != 0;

    if (has_invariant_tsc) {
        return "支持（恒定 TSC）";
    } else {
        return "不支持（TSC 可能随频率变化）";
    }
#else
    // 非 x86 架构，不支持 TSC
    return "不支持（非 x86 架构）";
#endif
}

// 通过 RDTSC (Read Time-Stamp Counter) 精确测量当前 CPU 的实际主频（单位 MHz）
inline double GetCPUFrequencyMHz() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    unsigned int eax, ebx, ecx, edx;
    uint64_t ts_start_us, ts_end_us, ts_temp_us, time_us, start_cycles, end_cycles;
    struct timeval ts;
    int has_invariant_tsc;
    double cpu_frequency_mhz;

    // 检查是否支持 Invariant TSC（恒定时间戳计数器）
    // 如果不支持，测量结果可能不可靠，但我们仍然尝试测量
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x80000007));
    has_invariant_tsc = edx & (1 << 8);
    // 注意：我们不在 runtime_info 中打印警告，保持静默
    (void)has_invariant_tsc;  // 消除未使用变量警告

    while (1) {
        gettimeofday(&ts, NULL);
        ts_temp_us = (ts.tv_usec + ts.tv_sec * 1000000);

        /* 等待微秒翻转以提高测量精度 */
        do {
            gettimeofday(&ts, NULL);
            start_cycles = RUNTIME_INFO_RDTSC();
            ts_start_us = (ts.tv_usec + ts.tv_sec * 1000000);
        } while (ts_start_us == ts_temp_us);

        /* 通过重新检查 gettimeofday 来防止在 gettimeofday 和 timing_start 之间发生上下文切换 */
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
        end_cycles = RUNTIME_INFO_RDTSC();
        ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
        time_us = ts_end_us - ts_start_us;
        if (time_us < kCalibrationTime) continue;

        /* 通过重新检查 gettimeofday 来防止在 gettimeofday 和 timing_end 之间发生上下文切换 */
        gettimeofday(&ts, NULL);
        ts_end_us = (ts.tv_usec + ts.tv_sec * 1000000);
        if (ts_end_us - ts_start_us > time_us) continue;

        break;
    }

    cpu_frequency_mhz = (double)(end_cycles - start_cycles) / (time_us);
    return cpu_frequency_mhz;
#else
    // 非 x86 架构，返回 0 表示不支持
    return 0.0;
#endif
}

// 获取CPU当前频率（使用精确的 RDTSC 测量方法，如果失败则回退到 /proc/cpuinfo）
// 平台支持：Linux（RDTSC + /proc/cpuinfo）、macOS（sysctl，部分支持）、Windows（不支持）
inline std::string GetCPUCurrentFreq() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // 尝试使用精确的 RDTSC 方法
    double freq_mhz = GetCPUFrequencyMHz();
    if (freq_mhz > 0) {
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << freq_mhz << " MHz";
        return oss.str();
    }
#endif

    // 回退方法：从 /proc/cpuinfo 读取
    std::string cpuinfo = ReadFile("/proc/cpuinfo");
    if (cpuinfo.empty()) {
        return "unknown";
    }

    std::istringstream iss(cpuinfo);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("cpu MHz") != std::string::npos) {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string freq = line.substr(colon_pos + 1);
                size_t start = freq.find_first_not_of(" \t");
                size_t end = freq.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    double freq_value = std::stod(freq.substr(start, end - start + 1));
                    std::ostringstream oss;
                    oss.precision(2);
                    oss << std::fixed << freq_value << " MHz";
                    return oss.str();
                }
            }
            break;
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    // macOS: 尝试使用 sysctl 获取 CPU 频率（可能不可用）
    uint64_t freq = 0;
    size_t size = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &size, nullptr, 0) == 0 && freq > 0) {
        double freq_mhz = freq / 1000000.0;
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << freq_mhz << " MHz";
        return oss.str();
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    // Windows: CPU 频率获取较复杂，需要 WMI 或性能计数器，这里返回不支持
    return "不支持";
#else
    return "unknown";
#endif
}

// 获取CPU最大频率（统一输出为MHz）
// 平台支持：Linux（/sys）、macOS（sysctl，部分支持）、Windows（不支持）
inline std::string GetCPUMaxFreq() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::string max_freq_file = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";
    std::string content = ReadFile(max_freq_file);
    if (!content.empty()) {
        // 移除换行符
        content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
        if (!content.empty()) {
            try {
                long long freq_khz = std::stoll(content);
                double freq_mhz = freq_khz / 1000.0;
                std::ostringstream oss;
                oss.precision(2);
                oss << std::fixed << freq_mhz << " MHz";
                return oss.str();
            } catch (...) {
                return "unknown";
            }
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    // macOS: 尝试使用 sysctl 获取最大 CPU 频率（可能不可用）
    uint64_t freq = 0;
    size_t size = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency_max", &freq, &size, nullptr, 0) == 0 && freq > 0) {
        double freq_mhz = freq / 1000000.0;
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << freq_mhz << " MHz";
        return oss.str();
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    // Windows: CPU 最大频率获取较复杂，需要 WMI，这里返回不支持
    return "不支持";
#else
    return "unknown";
#endif
}

// 获取CPU缓存大小
// 平台支持：Linux（/sys）、macOS（sysctl）、Windows（不支持）
inline std::string GetCPUCacheSize(int cache_level) {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::ostringstream path;
    path << "/sys/devices/system/cpu/cpu0/cache/index" << (cache_level - 1) << "/size";
    std::string content = ReadFile(path.str());
    if (!content.empty()) {
        // 移除换行符
        content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
        if (!content.empty()) {
            return content;
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    // macOS: 使用 sysctl 获取缓存大小
    uint64_t cache_size = 0;
    size_t size = sizeof(cache_size);
    std::string key;
    switch (cache_level) {
        case 1:
            key = "hw.l1icachesize";  // L1 指令缓存
            break;
        case 2:
            key = "hw.l2cachesize";   // L2 缓存
            break;
        case 3:
            key = "hw.l3cachesize";   // L3 缓存
            break;
        default:
            return "unknown";
    }
    if (sysctlbyname(key.c_str(), &cache_size, &size, nullptr, 0) == 0 && cache_size > 0) {
        // 转换为 KB
        uint64_t cache_kb = cache_size / 1024;
        return std::to_string(cache_kb) + "K";
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    // Windows: CPU 缓存信息获取较复杂，需要 WMI 或 CPUID，这里返回不支持
    return "不支持";
#else
    return "unknown";
#endif
}

// 获取CPU缓存信息（L1, L2, L3）
inline std::string GetCPUCacheInfo() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    struct CacheEntry {
        int level;
        std::string type;
        std::string size;
    };
    std::vector<CacheEntry> caches;
    const std::string base_path = "/sys/devices/system/cpu/cpu0/cache";
    const int max_index = 32;

    for (int idx = 0; idx < max_index; ++idx) {
        std::ostringstream level_path;
        level_path << base_path << "/index" << idx << "/level";
        std::ifstream level_file(level_path.str());
        if (!level_file.is_open()) {
            if (idx > 0) {
                break;
            }
            continue;
        }
        std::string level_line;
        std::getline(level_file, level_line);
        level_file.close();
        level_line = TrimWhitespace(level_line);
        if (level_line.empty()) {
            continue;
        }
        int level = 0;
        try {
            level = std::stoi(level_line);
        } catch (...) {
            continue;
        }

        std::ostringstream size_path;
        size_path << base_path << "/index" << idx << "/size";
        std::string size = TrimWhitespace(ReadFile(size_path.str()));
        if (size.empty()) {
            continue;
        }

        std::ostringstream type_path;
        type_path << base_path << "/index" << idx << "/type";
        std::string type = TrimWhitespace(ReadFile(type_path.str()));
        if (type.empty()) {
            type = "Unknown";
        }

        caches.push_back({level, type, size});
    }

    if (!caches.empty()) {
        std::sort(caches.begin(), caches.end(), [](const CacheEntry& a, const CacheEntry& b) {
            if (a.level != b.level) {
                return a.level < b.level;
            }
            if (a.type != b.type) {
                return a.type < b.type;
            }
            return a.size < b.size;
        });

        std::ostringstream oss;
        for (size_t i = 0; i < caches.size(); ++i) {
            if (i > 0) {
                oss << ", ";
            }
            std::string label = "L" + std::to_string(caches[i].level);
            std::string type = caches[i].type;
            if (type == "Data") {
                label += "d";
            } else if (type == "Instruction") {
                label += "i";
            } else if (type != "Unified") {
                label += "-" + type;
            }
            oss << label << ": " << caches[i].size;
        }
        return oss.str();
    }
#endif
    std::string l1 = GetCPUCacheSize(1);
    std::string l2 = GetCPUCacheSize(2);
    std::string l3 = GetCPUCacheSize(3);

    std::ostringstream oss;
    oss << "L1: " << l1;
    if (l2 != "unknown") {
        oss << ", L2: " << l2;
    }
    if (l3 != "unknown") {
        oss << ", L3: " << l3;
    }
    return oss.str();
}

// 获取CPU特性标志
// 平台支持：Linux（/proc/cpuinfo）、macOS（sysctl，部分支持）、Windows（CPUID，部分支持）
inline std::string GetCPUFlags() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::string cpuinfo = ReadFile("/proc/cpuinfo");
    if (cpuinfo.empty()) {
        return "unknown";
    }

    std::istringstream iss(cpuinfo);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("flags") != std::string::npos) {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string flags = line.substr(colon_pos + 1);
                // 去除前后空白
                size_t start = flags.find_first_not_of(" \t");
                size_t end = flags.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    flags = flags.substr(start, end - start + 1);

                    // 提取重要的 SIMD 相关标志
                    std::vector<std::string> important_flags = {
                        "avx512f", "avx512", "avx2", "avx", "sse4_2", "sse4.1",
                        "sse4", "sse3", "sse2", "sse", "mmx"
                    };

                    std::istringstream flags_stream(flags);
                    std::string flag;
                    std::vector<std::string> found_flags;

                    while (flags_stream >> flag) {
                        for (const auto& important : important_flags) {
                            if (flag.find(important) == 0) {
                                found_flags.push_back(flag);
                                break;
                            }
                        }
                    }

                    if (!found_flags.empty()) {
                        std::ostringstream oss;
                        oss << "SIMD支持: ";
                        for (size_t i = 0; i < found_flags.size(); ++i) {
                            if (i > 0) oss << ", ";
                            oss << found_flags[i];
                        }
                        return oss.str();
                    }

                    // 如果没找到重要标志，返回前100个字符
                    if (flags.length() > 100) {
                        return flags.substr(0, 100) + "...";
                    }
                    return flags;
                }
            }
            break;
        }
    }
    return "unknown";
#elif defined(RUNTIME_INFO_PLATFORM_MACOS)
    // macOS: 使用 sysctl 获取部分 CPU 特性（有限支持）
    std::ostringstream oss;
    oss << "SIMD支持: ";
    bool has_any = false;

    // 检查 AVX 支持
    int avx = 0;
    size_t size = sizeof(avx);
    if (sysctlbyname("hw.optional.avx1_0", &avx, &size, nullptr, 0) == 0 && avx) {
        if (has_any) oss << ", ";
        oss << "AVX";
        has_any = true;
    }

    // 检查 AVX2 支持
    if (sysctlbyname("hw.optional.avx2_0", &avx, &size, nullptr, 0) == 0 && avx) {
        if (has_any) oss << ", ";
        oss << "AVX2";
        has_any = true;
    }

    if (!has_any) {
        return "unknown";
    }
    return oss.str();
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS)
    // Windows: 使用 CPUID 检测 SIMD 支持（简化版本）
    // 注意：这需要内联汇编或 intrinsics，这里返回简化信息
    return "Windows 平台需要 CPUID 检测（未实现）";
#else
    return "unknown";
#endif
}

// 获取系统限制
// 平台支持：Linux（ulimit）、macOS（ulimit）、Windows（GetProcessWorkingSetSize）
inline std::string GetSystemLimit(const std::string& limit_type) {
#ifdef RUNTIME_INFO_PLATFORM_WINDOWS
    // Windows 不支持 ulimit，使用其他方法获取限制信息
    if (limit_type == "n") {
        // 文件描述符限制（Windows 使用句柄）
        return "Windows 使用句柄，限制较高";
    } else if (limit_type == "u") {
        // 进程/线程限制
        return "Windows 限制较高";
    } else if (limit_type == "v") {
        // 内存限制
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            std::ostringstream oss;
            oss << (memInfo.ullTotalVirtual / 1024 / 1024) << " MB";
            return oss.str();
        }
    }
    return "unknown";
#else
    std::string command = "ulimit -" + limit_type + " 2>/dev/null";
    std::string result = ExecuteCommand(command);
    if (!result.empty() && result != "-1") {
        return result;
    } else if (result == "-1") {
        return "unlimited";
    }
    return "unknown";
#endif
}

// 获取所有系统限制
inline std::string GetAllSystemLimits() {
    std::string fd_limit = GetSystemLimit("n");
    std::string proc_limit = GetSystemLimit("u");
    std::string mem_limit = GetSystemLimit("v");

    std::ostringstream oss;
    oss << "文件描述符: " << fd_limit;
    if (proc_limit != "unknown") {
        oss << ", 进程/线程: " << proc_limit;
    }
    if (mem_limit != "unknown") {
        oss << ", 内存: " << mem_limit;
    }
    return oss.str();
}

// 获取运行时的所有环境信息
// 动态库信息（仅 Linux）
#ifdef RUNTIME_INFO_PLATFORM_LINUX

struct MapEntryInfo {
    uintptr_t start;
    uintptr_t end;
    std::string perms;
    uint64_t offset;
};

struct SegmentInfo {
    uint64_t offset;
    uint64_t vaddr;
    uint64_t memsz;
    uint32_t flags;
};

struct LibraryDumpInfo {
    std::string path;
    std::string elf_header_info;
    std::vector<MapEntryInfo> maps;
    std::vector<SegmentInfo> segments;
};

inline std::string TrimPath(const std::string& raw) {
    if (raw.empty()) {
        return "";
    }
    size_t first = raw.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = raw.find_last_not_of(" \t");
    return raw.substr(first, last - first + 1);
}

inline bool ParseHex(const std::string& text, uint64_t& value) {
    try {
        value = std::stoull(text, nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

inline std::unordered_map<std::string, std::vector<MapEntryInfo>> CollectSharedLibraryMaps() {
    std::unordered_map<std::string, std::vector<MapEntryInfo>> libs;
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) {
        return libs;
    }

    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream iss(line);
        std::string addr_range, perms, offset, dev, inode;
        if (!(iss >> addr_range >> perms >> offset >> dev >> inode)) {
            continue;
        }
        std::string path_raw;
        std::getline(iss, path_raw);
        std::string path = TrimPath(path_raw);
        if (path.empty() || path.find(".so") == std::string::npos) {
            continue;
        }

        auto delim_pos = addr_range.find('-');
        if (delim_pos == std::string::npos) {
            continue;
        }
        uint64_t start = 0;
        uint64_t end = 0;
        if (!ParseHex(addr_range.substr(0, delim_pos), start) ||
            !ParseHex(addr_range.substr(delim_pos + 1), end)) {
            continue;
        }

        uint64_t file_offset = 0;
        if (!ParseHex(offset, file_offset)) {
            continue;
        }

        MapEntryInfo entry{
            static_cast<uintptr_t>(start),
            static_cast<uintptr_t>(end),
            perms,
            file_offset
        };
        libs[path].push_back(entry);
    }
    return libs;
}

inline std::string DescribeELFHeader(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "ELF头读取失败";
    }

    Elf64_Ehdr eh64;
    std::memset(&eh64, 0, sizeof(eh64));
    file.read(reinterpret_cast<char*>(&eh64), sizeof(eh64));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(Elf64_Ehdr))) {
        return "ELF头读取失败";
    }
    if (std::memcmp(eh64.e_ident, ELFMAG, SELFMAG) != 0) {
        return "非ELF文件";
    }

    auto format_common = [](uint16_t e_type, uint16_t e_machine) -> std::string {
        std::ostringstream oss;
        oss << "type=" << e_type << ", machine=" << e_machine;
        return oss.str();
    };

    std::ostringstream oss;
    unsigned char elf_class = eh64.e_ident[EI_CLASS];
    unsigned char elf_data = eh64.e_ident[EI_DATA];

    auto describe_data = [](unsigned char data) {
        switch (data) {
            case ELFDATA2LSB: return "LSB";
            case ELFDATA2MSB: return "MSB";
            default: return "Unknown";
        }
    };

    if (elf_class == ELFCLASS64) {
        oss << "ELF64, " << describe_data(elf_data) << ", "
            << format_common(eh64.e_type, eh64.e_machine)
            << ", entry=0x" << std::hex << eh64.e_entry
            << ", phoff=" << std::dec << eh64.e_phoff
            << ", shoff=" << eh64.e_shoff
            << ", phnum=" << eh64.e_phnum
            << ", shnum=" << eh64.e_shnum;
    } else if (elf_class == ELFCLASS32) {
        Elf32_Ehdr eh32;
        std::memset(&eh32, 0, sizeof(eh32));
        std::memcpy(&eh32, &eh64, sizeof(Elf32_Ehdr));
        oss << "ELF32, " << describe_data(elf_data) << ", "
            << format_common(eh32.e_type, eh32.e_machine)
            << ", entry=0x" << std::hex << eh32.e_entry
            << ", phoff=" << std::dec << eh32.e_phoff
            << ", shoff=" << eh32.e_shoff
            << ", phnum=" << eh32.e_phnum
            << ", shnum=" << eh32.e_shnum;
    } else {
        return "未知ELF类型";
    }
    return oss.str();
}

inline std::string FormatSegmentFlags(uint32_t flags) {
    std::vector<std::string> items;
    if (flags & PF_R) items.emplace_back("PF_R");
    if (flags & PF_W) items.emplace_back("PF_W");
    if (flags & PF_X) items.emplace_back("PF_X");
    if (items.empty()) {
        return "0";
    }
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            oss << "|";
        }
        oss << items[i];
    }
    return oss.str();
}

inline std::vector<SegmentInfo> ParseELFSegments(const std::string& path) {
    std::vector<SegmentInfo> segments;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return segments;
    }

    Elf64_Ehdr eh64;
    file.read(reinterpret_cast<char*>(&eh64), sizeof(eh64));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(Elf64_Ehdr))) {
        return segments;
    }
    if (std::memcmp(eh64.e_ident, ELFMAG, SELFMAG) != 0) {
        return segments;
    }

    unsigned char elf_class = eh64.e_ident[EI_CLASS];
    if (elf_class == ELFCLASS64) {
        if (eh64.e_phoff == 0 || eh64.e_phnum == 0) {
            return segments;
        }
        file.seekg(static_cast<std::streamoff>(eh64.e_phoff), std::ios::beg);
        for (int i = 0; i < eh64.e_phnum; ++i) {
            Elf64_Phdr ph;
            file.read(reinterpret_cast<char*>(&ph), sizeof(ph));
            if (!file) {
                break;
            }
            if (ph.p_type != PT_LOAD) {
                continue;
            }
            segments.push_back({ph.p_offset, ph.p_vaddr, ph.p_memsz, ph.p_flags});
        }
    } else if (elf_class == ELFCLASS32) {
        Elf32_Ehdr eh32;
        std::memcpy(&eh32, &eh64, sizeof(Elf32_Ehdr));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&eh32), sizeof(eh32));
        if (!file) {
            return segments;
        }
        if (eh32.e_phoff == 0 || eh32.e_phnum == 0) {
            return segments;
        }
        file.seekg(static_cast<std::streamoff>(eh32.e_phoff), std::ios::beg);
        for (int i = 0; i < eh32.e_phnum; ++i) {
            Elf32_Phdr ph;
            file.read(reinterpret_cast<char*>(&ph), sizeof(ph));
            if (!file) {
                break;
            }
            if (ph.p_type != PT_LOAD) {
                continue;
            }
            segments.push_back({
                static_cast<uint64_t>(ph.p_offset),
                static_cast<uint64_t>(ph.p_vaddr),
                static_cast<uint64_t>(ph.p_memsz),
                static_cast<uint32_t>(ph.p_flags)
            });
        }
    }
    return segments;
}

inline const SegmentInfo* MatchSegment(uint64_t file_offset, const std::vector<SegmentInfo>& segments) {
    for (const auto& seg : segments) {
        uint64_t seg_start = seg.offset;
        uint64_t seg_end = seg.offset + seg.memsz;
        if (file_offset >= seg_start && file_offset < seg_end) {
            return &seg;
        }
    }
    return nullptr;
}

inline std::vector<LibraryDumpInfo> BuildLibraryDump() {
    std::vector<LibraryDumpInfo> dumps;
    auto maps = CollectSharedLibraryMaps();
    for (auto& kv : maps) {
        LibraryDumpInfo info;
        info.path = kv.first;
        info.maps = std::move(kv.second);
        info.elf_header_info = DescribeELFHeader(info.path);
        info.segments = ParseELFSegments(info.path);
        dumps.push_back(std::move(info));
    }
    return dumps;
}

inline std::vector<std::string> DumpLoadedSharedLibraries(int verbose) {
    std::vector<std::string> lines;

    if (verbose <= 0) {
        return lines;
    }

    lines.push_back("------------ 已加载动态库 ------------");

    if (verbose < 3) {
        auto maps = CollectSharedLibraryMaps();
        if (maps.empty()) {
            lines.push_back("未找到可用的 .so 动态库或 /proc/self/maps 不可访问");
            lines.push_back("----------- 动态库列表结束 -----------");
            return lines;
        }

        for (const auto& kv : maps) {
            lines.push_back("库路径: " + kv.first);
        }

        lines.push_back("----------- 动态库列表结束 -----------");
        return lines;
    }

    auto dumps = BuildLibraryDump();
    if (dumps.empty()) {
        lines.push_back("未找到可用的 .so 动态库或 /proc/self/maps 不可访问");
        lines.push_back("---- 动态库列表结束 ----");
        return lines;
    }

    for (const auto& lib : dumps) {
        lines.push_back("库路径: " + lib.path);
        lines.push_back("  ELF头: " + lib.elf_header_info);

        if (lib.maps.empty()) {
            lines.push_back("  未找到映射区间");
            continue;
        }

        for (const auto& entry : lib.maps) {
            std::string segment_desc = "未知";
            if (!lib.segments.empty()) {
                auto seg = MatchSegment(entry.offset, lib.segments);
                if (seg) {
                    segment_desc = FormatSegmentFlags(seg->flags);
                }
            } else {
                if (entry.perms.find('x') != std::string::npos) {
                    segment_desc = "推测:PF_X";
                } else if (entry.perms.find('w') != std::string::npos) {
                    segment_desc = "推测:PF_W";
                } else if (entry.perms.find('r') != std::string::npos) {
                    segment_desc = "推测:PF_R";
                }
            }

            std::ostringstream line;
            line << "  段: " << segment_desc
                 << "  区间: 0x" << std::hex << entry.start
                 << "-0x" << entry.end
                 << "  权限: " << entry.perms;
            lines.push_back(line.str());
        }
    }

    lines.push_back("----------- 动态库列表结束 -----------");
    return lines;
}

#else

inline std::vector<std::string> DumpLoadedSharedLibraries(int verbose) {
    std::vector<std::string> lines;
    if (verbose <= 0) {
        return lines;
    }
    lines.push_back("------------ 已加载动态库 ------------");
    lines.push_back("当前平台不支持该功能，仅 Linux 可用");
    lines.push_back("----------- 动态库列表结束 -----------");
    return lines;
}

#endif

// 在 /proc/self/maps 中查找已映射的共享库路径（取首个匹配）。
inline std::string FindMappedSharedLibraryPath(const std::string& lib_name_substr) {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) {
        return "";
    }
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(lib_name_substr) == std::string::npos) {
            continue;
        }
        const size_t path_start = line.rfind(' ');
        if (path_start == std::string::npos) {
            continue;
        }
        std::string path = TrimWhitespace(line.substr(path_start));
        if (!path.empty() && path[0] != '[') {
            return path;
        }
    }
#endif
    return "";
}

// 从 ONNX Runtime 库路径中提取 gpu-x.y.z / cpu-x.y.z 标签。
inline std::string ExtractOnnxRuntimeTagFromPath(const std::string& path) {
    const char* tags[] = {"gpu-", "cpu-"};
    for (const char* tag : tags) {
        const size_t pos = path.find(tag);
        if (pos == std::string::npos) {
            continue;
        }
        const size_t ver_begin = pos;
        size_t ver_end = ver_begin + std::strlen(tag);
        while (ver_end < path.size()) {
            const char c = path[ver_end];
            if ((c >= '0' && c <= '9') || c == '.') {
                ++ver_end;
            } else {
                break;
            }
        }
        if (ver_end > ver_begin + std::strlen(tag)) {
            return path.substr(ver_begin, ver_end - ver_begin);
        }
    }
    return "";
}

// ONNX Runtime：未加载返回 none；合并路径 tag 与 OrtGetVersionString。
inline std::string GetRuntimeOnnxRuntimeVersion() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    const std::string lib_path = FindMappedSharedLibraryPath("libonnxruntime.so");
    if (lib_path.empty()) {
        return "none";
    }
    const std::string tag = ExtractOnnxRuntimeTagFromPath(lib_path);
    std::string api_ver;
    struct OrtApiBaseLite {
        const void* (*GetApi)(uint32_t version);
        const char* (*GetVersionString)(void);
    };
    using OrtGetApiBaseFn = const OrtApiBaseLite* (*)(void);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    OrtGetApiBaseFn ort_get_api_base =
        reinterpret_cast<OrtGetApiBaseFn>(dlsym(RTLD_DEFAULT, "OrtGetApiBase"));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    if (ort_get_api_base) {
        const OrtApiBaseLite* api_base = ort_get_api_base();
        if (api_base && api_base->GetVersionString) {
            const char* ver = api_base->GetVersionString();
            if (ver && ver[0] != '\0') {
                api_ver = ver;
            }
        }
    }
    if (!tag.empty() && !api_ver.empty()) {
        if (tag == api_ver) {
            return tag;
        }
        return tag + " (" + api_ver + ")";
    }
    if (!tag.empty()) {
        return tag;
    }
    if (!api_ver.empty()) {
        return api_ver;
    }
    return lib_path;
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS) || defined(RUNTIME_INFO_PLATFORM_MACOS)
    return "unknown";
#else
    return "unknown";
#endif
}

// CUDA：依据已加载的 libcudart.so soname；未加载返回 none。
inline std::string GetRuntimeCudaVersion() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    const std::string lib_path = FindMappedSharedLibraryPath("libcudart.so");
    if (lib_path.empty()) {
        return "none";
    }
    const size_t pos = lib_path.find("libcudart.so");
    if (pos != std::string::npos) {
        return lib_path.substr(pos);
    }
    return lib_path;
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS) || defined(RUNTIME_INFO_PLATFORM_MACOS)
    return "unknown";
#else
    return "unknown";
#endif
}

// cuDNN：已加载时优先 cudnnGetVersion()；未加载返回 none。
inline std::string GetRuntimeCudnnVersion() {
#ifdef RUNTIME_INFO_PLATFORM_LINUX
    const std::string lib_path = FindMappedSharedLibraryPath("libcudnn.so");
    if (lib_path.empty()) {
        return "none";
    }
    using CudnnGetVersionFn = size_t (*)(void);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    CudnnGetVersionFn cudnn_get_version =
        reinterpret_cast<CudnnGetVersionFn>(dlsym(RTLD_DEFAULT, "cudnnGetVersion"));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    if (cudnn_get_version) {
        const size_t encoded = cudnn_get_version();
        const int major = static_cast<int>(encoded / 1000);
        const int minor = static_cast<int>((encoded % 1000) / 100);
        const int patch = static_cast<int>(encoded % 100);
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
    const size_t pos = lib_path.find("libcudnn.so");
    if (pos != std::string::npos) {
        return lib_path.substr(pos);
    }
    return lib_path;
#elif defined(RUNTIME_INFO_PLATFORM_WINDOWS) || defined(RUNTIME_INFO_PLATFORM_MACOS)
    return "unknown";
#else
    return "unknown";
#endif
}

// 获取运行时信息
// verbose: 0-不输出动态库列表，1-输出动态库列表，2-输出动态库列表详细信息
inline std::vector<std::string> GetRuntimeInfo(int verbose = 0) {
    std::vector<std::string> info;
    info.push_back("=========== 运行环境 信息 ===========");

    // 系统信息
    info.push_back("操作系统: " + SafeStringCall(GetOSVersion));
    info.push_back("内核版本: " + SafeStringCall(GetKernelVersion));
    info.push_back("系统架构: " + SafeStringCall(GetSystemArchitecture));

    // CPU信息
    info.push_back("CPU型号: " + SafeStringCall(GetCPUModel));
    info.push_back("恒定TSC支持: " + SafeStringCall(GetTSCSupport));
    info.push_back("CPU核心数: " + SafeStringCall(GetCPUCores));
    info.push_back("CPU线程数: " + SafeStringCall(GetCPUThreads));
    info.push_back("CPU当前频率: " + SafeStringCall(GetCPUCurrentFreq));
    info.push_back("CPU最大频率: " + SafeStringCall(GetCPUMaxFreq));
    info.push_back("CPU缓存: " + SafeStringCall(GetCPUCacheInfo));
    info.push_back("CPU特性: " + SafeStringCall(GetCPUFlags));

    // 内存信息
    info.push_back("内存信息: " + SafeStringCall(GetMemoryInfo));
    info.push_back("透明大页(THP): " + SafeStringCall(GetTransparentHugePageStatus));

    // 系统限制
    info.push_back("系统限制: " + SafeStringCall(GetAllSystemLimits));

    // 环境变量和库路径
    info.push_back("LD_LIBRARY_PATH: " + SafeStringCall(GetLibraryPath));

    info.push_back("ONNX Runtime: " + SafeStringCall(GetRuntimeOnnxRuntimeVersion));
    info.push_back("CUDA: " + SafeStringCall(GetRuntimeCudaVersion));
    info.push_back("cuDNN: " + SafeStringCall(GetRuntimeCudnnVersion));

    // 动态库列表（根据 verbose 控制输出粒度）
    if (verbose > 0) {
        auto so_dump = SafeVectorCall([&]() {
            return DumpLoadedSharedLibraries(verbose);
        }, std::vector<std::string>{"动态库信息: 检测失败"});
        info.insert(info.end(), so_dump.begin(), so_dump.end());
    }

    info.push_back("=====================================");
    return info;
}

// 运行时信息输出函数
// verbose: 0-不输出动态库列表，1-输出动态库列表，2-输出动态库列表详细信息
inline void PrintRuntimeInfo(int verbose = 0) {
    auto info = GetRuntimeInfo(verbose);
    for (const auto& line : info) {
        std::cout << line << std::endl;
    }
}

} // namespace runtime_info
} // namespace velatools
