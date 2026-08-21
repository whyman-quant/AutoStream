#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define BUILD_INFO_PLATFORM_WINDOWS
#elif defined(__APPLE__)
    #define BUILD_INFO_PLATFORM_MACOS
    #include <dlfcn.h>
    #include <mach-o/dyld.h>
    #include <unistd.h>
#elif defined(__linux__)
    #define BUILD_INFO_PLATFORM_LINUX
    #include <dlfcn.h>
    #include <linux/limits.h>
    #include <unistd.h>
#endif

// 将宏展开后再转为字符串（用于回退到编译器内置宏时）
#define BUILD_INFO_XSTR(x) #x
#define BUILD_INFO_STR(x) BUILD_INFO_XSTR(x)

namespace velatools {
namespace build_info {

// 执行 shell 命令并捕获首段输出（供 GetRPATH 使用）
inline std::string ExecuteCommand(const std::string& command) {
#ifdef BUILD_INFO_PLATFORM_WINDOWS
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
#ifdef BUILD_INFO_PLATFORM_WINDOWS
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

namespace detail {

inline int BuildInfoImageTag() { return 0; }

inline std::string TrimCommandOutput(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    const size_t start = text.find_first_not_of(" \t");
    const size_t end = text.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        return text.substr(start, end - start + 1);
    }
    return text;
}

inline std::string ParseRpathToolOutput(const std::string& raw) {
    const std::string line = TrimCommandOutput(raw);
    if (line.empty()) {
        return "";
    }
    const size_t lb = line.find('[');
    const size_t rb = line.rfind(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
        return line.substr(lb + 1, rb - lb - 1);
    }
    return line;
}

// 解析当前代码所在映像路径：dlopen 的 .so 用 dladdr；主程序回退 /proc/self/exe 等
inline std::string ResolveImagePathForRpath() {
#if defined(BUILD_INFO_PLATFORM_LINUX) || defined(BUILD_INFO_PLATFORM_MACOS)
    Dl_info info;
    std::memset(&info, 0, sizeof(info));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    const bool dladdr_ok =
        dladdr(reinterpret_cast<void*>(&BuildInfoImageTag), &info) != 0 && info.dli_fname != nullptr &&
        info.dli_fname[0] != '\0';
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    if (dladdr_ok) {
        return std::string(info.dli_fname);
    }

#endif
    char image_path[PATH_MAX];
#if defined(BUILD_INFO_PLATFORM_LINUX)
    const ssize_t len = readlink("/proc/self/exe", image_path, sizeof(image_path) - 1);
    if (len <= 0) {
        return "";
    }
    image_path[len] = '\0';
#elif defined(BUILD_INFO_PLATFORM_MACOS)
    uint32_t size = sizeof(image_path);
    if (_NSGetExecutablePath(image_path, &size) != 0) {
        return "";
    }
#else
    return "";
#endif
    return std::string(image_path);
}

inline std::string ReadEmbeddedRpathFromImage(const std::string& image_path) {
    if (image_path.empty()) {
        return "";
    }
    std::string command = "readelf -d \"" + image_path +
                          "\" 2>/dev/null | grep -E '(RPATH|RUNPATH)' | head -1";
    std::string result = ParseRpathToolOutput(ExecuteCommand(command));
    if (!result.empty()) {
        return result;
    }
    command = "objdump -p \"" + image_path +
              "\" 2>/dev/null | grep -E '(RPATH|RUNPATH)' | head -1";
    return ParseRpathToolOutput(ExecuteCommand(command));
}

} // namespace detail

// 读取当前映像嵌入的 RPATH/RUNPATH（链接期写入；Python dlopen .so 时读 .so 而非解释器）
inline std::string GetRPATH() {
#ifdef BUILD_INFO_PLATFORM_WINDOWS
    return "Windows 使用 PATH 环境变量";
#else
    const std::string image_path = detail::ResolveImagePathForRpath();
    const std::string rpath = detail::ReadEmbeddedRpathFromImage(image_path);
    if (!rpath.empty()) {
        return rpath;
    }
    return "未知";
#endif
}

// 获取构建信息，返回每一行的内容
// 逻辑：优先使用 BUILD_INFO_ 前缀的宏（由构建脚本/CMake 注入）；若未定义，则尝试使用
// 编译器预定义宏作为回退。编译器常见预定义宏包括：__DATE__、__TIME__（编译时间）、
// __VERSION__（GCC/Clang 版本）、__cplusplus（C++ 标准版本号）等。
inline std::vector<std::string> GetBuildInfo() {
    std::vector<std::string> info;
    info.push_back("============== 构建信息 ==============");

    // 构建时间：优先 BUILD_INFO_BUILD_TIMESTAMP，否则若存在 __DATE__/__TIME__ 则用其回退
#ifdef BUILD_INFO_BUILD_TIMESTAMP
    info.push_back("构建时间: " + std::string(BUILD_INFO_BUILD_TIMESTAMP));
#elif defined(__DATE__) && defined(__TIME__)
    info.push_back(std::string("构建时间: ") + __DATE__ + " " + __TIME__ + " (编译器默认)");
#else
    info.push_back("构建时间: 未定义");
#endif

#ifdef BUILD_INFO_GIT_REPO_NAME
    info.push_back("Git仓库: " + std::string(BUILD_INFO_GIT_REPO_NAME));
#else
    info.push_back("Git仓库: 未定义");
#endif

#ifdef BUILD_INFO_GIT_BRANCH
    info.push_back("Git分支: " + std::string(BUILD_INFO_GIT_BRANCH));
#else
    info.push_back("Git分支: 未定义");
#endif

#ifdef BUILD_INFO_GIT_COMMIT
    info.push_back("Git提交: " + std::string(BUILD_INFO_GIT_COMMIT));
#else
    info.push_back("Git提交: 未定义");
#endif

#ifdef BUILD_INFO_GIT_STATUS
    info.push_back("Git状态: " + std::string(BUILD_INFO_GIT_STATUS));
#else
    info.push_back("Git状态: 未定义");
#endif

#ifdef BUILD_INFO_CMAKE_VERSION
    info.push_back("CMake版本: " + std::string(BUILD_INFO_CMAKE_VERSION));
#else
    info.push_back("CMake版本: 未定义");
#endif

#ifdef BUILD_INFO_MAKE_VERSION
    info.push_back("Make版本: " + std::string(BUILD_INFO_MAKE_VERSION));
#else
    info.push_back("Make版本: 未定义");
#endif

#ifdef BUILD_INFO_GXX_CMD_VERSION
    info.push_back("g++版本: " + std::string(BUILD_INFO_GXX_CMD_VERSION));
#else
    info.push_back("g++版本: 未定义");
#endif

#ifdef BUILD_INFO_GCC_CMD_VERSION
    info.push_back("gcc版本: " + std::string(BUILD_INFO_GCC_CMD_VERSION));
#else
    info.push_back("gcc版本: 未定义");
#endif

// C++ 编译器版本：优先 BUILD_INFO_CXX_COMPILER_VERSION，否则若存在 __VERSION__ 则用其回退（GCC/Clang）
#ifdef BUILD_INFO_CXX_COMPILER_VERSION
    info.push_back("C++编译器版本: " + std::string(BUILD_INFO_CXX_COMPILER_VERSION));
#elif defined(__VERSION__)
    info.push_back(std::string("C++编译器版本: ") + __VERSION__ + " (编译器默认)");
#else
    info.push_back("C++编译器版本: 未定义");
#endif

    // C++ 标准：优先 BUILD_INFO_CXX_STANDARD，否则若存在 __cplusplus 则用其回退
#ifdef BUILD_INFO_CXX_STANDARD
    info.push_back("C++标准: " + std::string(BUILD_INFO_CXX_STANDARD));
#elif defined(__cplusplus)
    info.push_back(std::string("C++标准: ") + BUILD_INFO_STR(__cplusplus) + " (编译器默认)");
#else
    info.push_back("C++标准: 未定义");
#endif

#ifdef BUILD_INFO_CPU_MODEL
    info.push_back("CPU型号: " + std::string(BUILD_INFO_CPU_MODEL));
#else
    info.push_back("CPU型号: 未定义");
#endif

#ifdef BUILD_INFO_ONNX_RUNTIME
    info.push_back("ONNX Runtime: " + std::string(BUILD_INFO_ONNX_RUNTIME));
#else
    info.push_back("ONNX Runtime: 未定义");
#endif

#ifdef BUILD_INFO_CUDA_VERSION
    info.push_back("CUDA: " + std::string(BUILD_INFO_CUDA_VERSION));
#else
    info.push_back("CUDA: 未定义");
#endif

#ifdef BUILD_INFO_CUDNN_VERSION
    info.push_back("cuDNN: " + std::string(BUILD_INFO_CUDNN_VERSION));
#else
    info.push_back("cuDNN: 未定义");
#endif

#ifdef BUILD_INFO_COMPILE_FLAGS
    info.push_back("编译标志: " + std::string(BUILD_INFO_COMPILE_FLAGS));
#else
    info.push_back("编译标志: 未定义");
#endif

    info.push_back("RPATH/RUNPATH: " + GetRPATH());

    // NDEBUG：Release 构建时通常由 CMake/构建系统定义，未定义则视为 Debug
#ifdef NDEBUG
    info.push_back("NDEBUG: 已定义 (Release)");
#else
    info.push_back("NDEBUG: 未定义 (Debug)");
#endif

    info.push_back("=====================================");
    return info;
}

// 构建信息输出函数
inline void PrintBuildInfo() {
    auto info = GetBuildInfo();
    for (const auto& line : info) {
        std::cout << line << std::endl;
    }
}

} // namespace build_info
} // namespace velatools

#undef BUILD_INFO_STR
#undef BUILD_INFO_XSTR
