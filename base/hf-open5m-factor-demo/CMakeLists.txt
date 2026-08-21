#=============================================================================
# scl 与 GCC 工具集
#=============================================================================
# 查看本机已安装、可直接 scl enable 的软件集合名：
#   scl -l
# 列出的每一项（如 devtoolset-11）即为「集合名」。
#
# 临时启用某套工具链（仅对紧跟的一条命令生效）：
#   scl enable <集合名> '<命令>'
# 会在执行该命令时把 PATH、MANPATH、LD_LIBRARY_PATH 等调整为优先使用集合内程序。
#
# 常见用法：进入子 shell，在 shell 内持续使用 devtoolset 里的 gcc 等，退出后恢复系统默认：
#   scl enable devtoolset-11 bash
# （将 devtoolset-11 换成 scl -l 中实际存在的集合名，例如 devtoolset-9。）
#
# 如何复原（回到未启用 scl 集合前的环境）：
# - 单行形式 `scl enable <集合名> '<命令>'`：只影响子进程里的那一条命令，当前交互式 shell 未改，无需复原。
# - 子 shell 形式：在 `scl enable ... bash`（或 `sh -l`）开启的会话里执行 `exit`、或按 Ctrl+D 退出该 shell，
#   即回到上一层 shell，PATH 等与进入前一致。
# - 若已在某终端里改乱了环境：另开一个干净终端登录，或在该终端执行 `exec bash -l`（慎用，会替换当前 shell）
#   以按登录脚本重新初始化（具体行为取决于 ~/.bashrc、~/.bash_profile 等）。
#
# 重要提示：切换 gcc / 工具链后，需要清除 CMake 缓存并重新配置
# CMake 会在首次配置时缓存编译器路径（见 build/CMakeCache.txt），
# 即使已在 scl 环境中换了编译器，仍可能沿用旧缓存。
# 推荐做法：删除 build 目录后，在期望的 scl 环境下重新 cmake 配置。

#=============================================================================
# 编译器配置
#=============================================================================

# 强制 CMake 使用当前环境中的编译器（避免缓存问题）
# 注意：通过 scl 启用某套 GCC 工具集（如 devtoolset-11）后，须保证当前环境中 CC/CXX 已设置，
# 或 PATH 能解析到期望的 gcc/g++；否则 CMake 仍可能按旧 PATH 选到系统默认编译器。
# 优先尊重 cmake -DCMAKE_*_COMPILER= 或已缓存的编译器；否则用环境变量 CC/CXX，再回退 which。
if(CMAKE_C_COMPILER)
    message(STATUS "Using C compiler from CMake cache/args: ${CMAKE_C_COMPILER}")
elseif(DEFINED ENV{CC})
    set(CMAKE_C_COMPILER "$ENV{CC}" CACHE PATH "C compiler" FORCE)
    message(STATUS "Using C compiler from environment: $ENV{CC}")
else()
    execute_process(
        COMMAND which gcc
        OUTPUT_VARIABLE GCC_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GCC_PATH)
        set(CMAKE_C_COMPILER "${GCC_PATH}" CACHE PATH "C compiler" FORCE)
        message(STATUS "Using C compiler from PATH: ${GCC_PATH}")
    else()
        find_program(CC_PATH gcc)
        if(CC_PATH)
            set(CMAKE_C_COMPILER "${CC_PATH}" CACHE PATH "C compiler" FORCE)
            message(STATUS "Using C compiler (fallback): ${CC_PATH}")
        endif()
    endif()
endif()

if(CMAKE_CXX_COMPILER)
    message(STATUS "Using C++ compiler from CMake cache/args: ${CMAKE_CXX_COMPILER}")
elseif(DEFINED ENV{CXX})
    set(CMAKE_CXX_COMPILER "$ENV{CXX}" CACHE PATH "C++ compiler" FORCE)
    message(STATUS "Using C++ compiler from environment: $ENV{CXX}")
else()
    execute_process(
        COMMAND which g++
        OUTPUT_VARIABLE GXX_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GXX_PATH)
        set(CMAKE_CXX_COMPILER "${GXX_PATH}" CACHE PATH "C++ compiler" FORCE)
        message(STATUS "Using C++ compiler from PATH: ${GXX_PATH}")
    else()
        find_program(CXX_PATH g++)
        if(CXX_PATH)
            set(CMAKE_CXX_COMPILER "${CXX_PATH}" CACHE PATH "C++ compiler" FORCE)
            message(STATUS "Using C++ compiler (fallback): ${CXX_PATH}")
        endif()
    endif()
endif()

# 标记编译器已被强制设置，避免 project() 调用时重新检测
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# 如果编译器路径包含 /usr/local/gcc-，自动添加其库路径到编译环境
# 这可以解决 GCC 依赖库（如 libmpfr.so.6）找不到的问题
if(CMAKE_C_COMPILER MATCHES "/usr/local/gcc-[^/]+")
    string(REGEX REPLACE "/bin/gcc$" "" GCC_BASE_DIR "${CMAKE_C_COMPILER}")
    set(GCC_LIB_DIR "${GCC_BASE_DIR}/lib64")
    if(EXISTS "${GCC_LIB_DIR}")
        # 设置运行时库搜索路径
        set(CMAKE_BUILD_RPATH "${GCC_LIB_DIR}")
        set(CMAKE_INSTALL_RPATH "${GCC_LIB_DIR}")
        list(APPEND CMAKE_BUILD_RPATH "${GCC_BASE_DIR}/lib")
        list(APPEND CMAKE_INSTALL_RPATH "${GCC_BASE_DIR}/lib")

        # 设置编译时的环境变量，确保编译器工具链能找到依赖库
        # 注意：CMake 的 set(ENV{...}) 不会传递到编译命令中
        # 需要在编译时手动设置 LD_LIBRARY_PATH，或者使用 CMAKE_C_COMPILER_LAUNCHER
        # 但更简单的方法是在 Makefile 或编译命令中设置环境变量

        message(STATUS "Added GCC library path to RPATH: ${GCC_LIB_DIR}")
        # 检测 GCC 版本并提示可能的依赖库问题
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "13.0")
                message(STATUS "⚠️  检测到 GCC ${CMAKE_CXX_COMPILER_VERSION}，可能需要 libmpfr.so.6")
                message(STATUS "⚠️  如果编译时出现 'libmpfr.so.6: cannot open shared object file' 错误，")
                message(STATUS "⚠️  Makefile 已自动设置 LD_LIBRARY_PATH，如仍有问题请检查 GCC 安装是否完整")
            endif()
        endif()
    endif()
endif()

#=============================================================================
# 项目配置
#=============================================================================

cmake_minimum_required(VERSION 3.10)
project(hf-stock-cross-section-framework)

message(STATUS "CMake version: ${CMAKE_VERSION}")

#=============================================================================
# 构建配置
#=============================================================================

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "构建类型: Debug/Release" FORCE)
endif()
# 说明：Release 模式下的 -DNDEBUG 来自 CMake 默认变量 CMAKE_CXX_FLAGS_RELEASE，
# 这里的 add_compile_options(...) 是在该默认值基础上追加项目自定义选项。

# C++ 标准：顶层 Makefile 通过 -DCMAKE_CXX_STANDARD= 传入；未在命令行指定时默认 11
# （与 Makefile 中 DEFAULT_CMAKE_CXX_STANDARD 保持一致；仅改默认时改 Makefile 中该变量）
#
# 各标准对 GCC/g++ 的最低版本要求（以 GNU 官方 C++ 支持为准；指编译期语言特性，与下方 glibc 无关）：
#   C++11 → g++ ≥ 4.8.1（4.8 仍有少量特性不完整；CentOS 7 自带 4.8.x 可覆盖本工程默认 11）
#   C++14 → g++ ≥ 5.1
#   C++17 → g++ ≥ 7.1（建议 ≥ 8；第三方如 ONNX Runtime 1.24.x 头文件亦按 C++17 编写）
#   C++20 → g++ ≥ 10.1（建议 ≥ 11；CACHE 可写 20，Makefile 默认链路未系统验证）
# RHEL/CentOS 7 若系统 g++ 过旧，可用 devtoolset/scl 在 shell 环境中切换更高版本 g++ 后再编译。
#
# ---------- C++ 标准如何作用到各目标（穿透原理） ----------
# 本处设置的 CMAKE_CXX_STANDARD / CMAKE_CXX_STANDARD_REQUIRED / CMAKE_CXX_EXTENSIONS 作用于
# 当前目录及后续 add_subdirectory 树：对「未单独设置目标属性 CXX_STANDARD」的 C++ 目标，
# CMake 会以上述变量为默认，在编译时生成对应的 -std=c++NN（即工程级穿透）。
#
# - app_live（strategy）、app_factor/main、app_model/main 等：CMakeLists 中未写 CXX_STANDARD，
#   依赖本机制跟随 make build CMAKE_CXX_STANDARD=… 传入的值。
# - factors_* / models_* / factors_comm / models_comm：在模板或 factors|models/CMakeLists.txt 中
#   通过 hf_apply_module_cxx_standard / hf_apply_project_cxx_standard 显式设置 CXX_STANDARD（可覆盖工程默认）。
# - 例外：app_live/comm/zipper 在下方对 staticZipper 等目标固定 CXX_STANDARD 11，不跟随本处工程标准。
# 与 g++/Clang、CMake 版本是否匹配：仅在下方 include 后检查一次（make build 走 cmake .. 即可触发，Makefile 不再重复 -P 预检）。
set(CMAKE_CXX_STANDARD 11 CACHE STRING "C++ standard (e.g. 11, 14, 17, 20)")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")

# ONNX Runtime 全局版本（Makefile: ONNX_RUNTIME=gpu-1.19.2）；所有 models/* ONNX 模型统一使用，禁止单模型覆盖
set(ONNX_RUNTIME "gpu-1.19.2" CACHE STRING
    "ONNX Runtime 版本标签（如 gpu-1.19.2 → onnxruntime-linux-x64-gpu-1.19.2）")
message(STATUS "ONNX Runtime (ONNX_RUNTIME): ${ONNX_RUNTIME}")

include(${CMAKE_SOURCE_DIR}/cmake/CxxStandardCompatCheck.cmake)
# 使用 project() 之后 CMake 已选定的 CMAKE_CXX_COMPILER_*，与真实编译一致
hf_warn_cxx_standard_toolchain_compat("${CMAKE_CXX_STANDARD}")

# -----------------------------------------------------------------------------
# 系统 C/C++ 运行库与 GCC 工具集（scl / devtoolset）的关系（重要）
# -----------------------------------------------------------------------------
# - **glibc**：GNU C 库，由操作系统发行版与用户态基座提供，版本与 **当前系统** 强绑定。仅用
#   `scl enable` 切换 **GCC 编译器前端** 并不会把整台机器的 glibc 换成另一套；可执行文件在目标机上
#   运行时仍依赖 **目标机** 的系统 C 库。跨 OS 大版本或跨发行版部署时，须以目标环境 glibc 为准。
#   若第三方 **预编译** 库要求 **更高版本的 glibc**（例如 `ldd` 报 `GLIBC_x.xx not found`），
#   仅靠换 **scl/devtoolset** 或单独换编译器 **无法** 把系统 glibc 升到该版本；可行手段只有
#   **更换到 glibc 已满足要求的机器**，或对当前环境做 **操作系统升级 / 大版本迁移** 等发行版级升级。
# - **libstdc++** 与 **libc++**：二者是不同的 C++ 标准库实现。本工程以 GCC 为主时链接的是 **GNU
#   libstdc++**；**libc++** 为 LLVM/Clang 生态常用，与本仓库默认 GCC 链路不是同一套，勿混为一谈。
#   devtoolset 可能携带较新的 `libstdc++.so`，与下方 `_GLIBCXX_USE_CXX11_ABI` 及运行时的
#   `LD_LIBRARY_PATH`/RPATH 有关，但进程仍加载 **本机 glibc + 动态链接器**，这一层 **不因只换
#   工具集集合名** 就变成另一套操作系统 ABI。
# 结论：换 GCC 工具集主要改变 **编译器与可见工具链路径**；**与系统绑定的 C 运行时（glibc）**
#   不会因 scl 切换而「整体替换」；与第三方预编译 `.so` 混链时仍须核对 **libstdc++ 符号与 ABI**。

# libstdc++ 双 ABI（_GLIBCXX_USE_CXX11_ABI，与 glibc 版本无关）：
# - 0：std::string 等为旧版内存布局（simu 预编译 st_wrapper 使用此布局填充 st_config_t）。
# - 1：GCC 5.1+ 新布局（符号常带 std::__cxx11）；本机新 GCC 默认倾向 1。
# app_live 经 simu 跨 so 传递 st_config_t，须与 st_wrapper 一致，故 Makefile 的 build-live 传 -DGLIBCXX_CXX11_ABI=0。
# app_factor/app_model 为独立可执行文件，Makefile 的 build-factor/build-model 传 1。
# 未改 ABI 就切换 build-live 与 build-factor 时，建议 make clean 以免 CMakeCache 残留旧值。
if(NOT DEFINED GLIBCXX_CXX11_ABI)
    set(GLIBCXX_CXX11_ABI 1)
endif()
set(GLIBCXX_CXX11_ABI ${GLIBCXX_CXX11_ABI} CACHE STRING "Set _GLIBCXX_USE_CXX11_ABI (0 or 1)")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=${GLIBCXX_CXX11_ABI})
    message(STATUS "Using _GLIBCXX_USE_CXX11_ABI=${GLIBCXX_CXX11_ABI}")
endif()

# 构建目标选项
# 选项BUILD_APP_LIVE用于控制是否构建实时应用程序，ON表示默认启用（即默认会构建）
# 注释说明：BUILD_APP_LIVE 选项是在 CMakeLists.txt（本文件）中通过 option(BUILD_APP_LIVE ...) 命令设置的。
# Makefile 并未直接设置 BUILD_APP_LIVE，而是通过调用 CMake 时传递 -DBUILD_APP_LIVE=ON 或 OFF，将该选项传递给 CMake 配置阶段。
# 例如，Makefile 的 build_target 会用 $(CMAKE) .. $(1) 进行 CMake 配置，$(1) 就包括了 -DBUILD_APP_LIVE=ON。
# 因此 BUILD_APP_LIVE 的定义和默认值在本 CMakeLists.txt 中。
# 换句话说，BUILD_APP_LIVE 是 CMake 项目的选项，Makefile 通过调用 CMake 并传递参数间接控制其值。
option(BUILD_APP_LIVE "构建Live版应用程序" ON)
option(BUILD_APP_FACTOR "构建因子本地版应用程序" OFF)
option(BUILD_APP_MODEL "构建模型本地版应用程序" OFF)
# 选项TIME_STATS用于控制是否启用时间统计，OFF表示默认不启用
option(TIME_STATS "是否开启计时统计" OFF)
# 选项ENABLE_NATIVE_MARCH用于控制是否启用 -march=native，OFF表示默认不启用
option(ENABLE_NATIVE_MARCH "是否开启 -march=native 优化" OFF)
# 为 ON 时：不向 Release 编译标志追加 -mavx512f，即使编译器与 CPU 检测均支持 AVX-512。
# 适用于需与旧 GCC（不支持 -mavx512f）行为对齐或排查 AVX-512 相关运行问题时。
option(DISABLE_AVX512 "强制禁用 AVX-512（不追加 -mavx512f）" OFF)

# 确保有且仅有一个APP_构建目标
# 检查 APP_ 构建目标，确保有且仅有一个为 ON
set(APP_TARGET_COUNT 0)
if(BUILD_APP_LIVE)
    add_definitions(-DENABLE_APP_LIVE)
    math(EXPR APP_TARGET_COUNT "${APP_TARGET_COUNT}+1")
endif()
if(BUILD_APP_FACTOR)
    math(EXPR APP_TARGET_COUNT "${APP_TARGET_COUNT}+1")
endif()
if(BUILD_APP_MODEL)
    math(EXPR APP_TARGET_COUNT "${APP_TARGET_COUNT}+1")
endif()
if(APP_TARGET_COUNT LESS 1)
    message(FATAL_ERROR "必须选择一个构建目标 (BUILD_APP_LIVE, BUILD_APP_FACTOR, BUILD_APP_MODEL) 为 ON")
elseif(APP_TARGET_COUNT GREATER 1)
    message(FATAL_ERROR "有且只能选择一个构建目标为 ON，当前选择了 ${APP_TARGET_COUNT} 个")
endif()

#=============================================================================
# 策略会话模式配置（STRATEGY_SESSION_MODE）
#=============================================================================

# OPEN：盘前集合竞价；CLOSE：收盘集合竞价；CONTINUOUS：盘中连续竞价
if(NOT DEFINED STRATEGY_SESSION_MODE)
    set(STRATEGY_SESSION_MODE "OPEN" CACHE STRING "策略会话模式: OPEN, CLOSE, CONTINUOUS" FORCE)
endif()

# 定义三个独立的宏变量，C++代码用#if defined判断
if(STRATEGY_SESSION_MODE STREQUAL "OPEN")
    add_definitions(-DENABLE_STRATEGY_SESSION_MODE_OPEN)
elseif(STRATEGY_SESSION_MODE STREQUAL "CLOSE")
    add_definitions(-DENABLE_STRATEGY_SESSION_MODE_CLOSE)
elseif(STRATEGY_SESSION_MODE STREQUAL "CONTINUOUS")
    add_definitions(-DENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
else()
    message(FATAL_ERROR "Unknown STRATEGY_SESSION_MODE: ${STRATEGY_SESSION_MODE}, 可选值为: OPEN, CLOSE, CONTINUOUS")
endif()

string(ASCII 27 _ANSI_ESC)
message(STATUS "STRATEGY_SESSION_MODE set to ${_ANSI_ESC}[32m${STRATEGY_SESSION_MODE}${_ANSI_ESC}[0m")

#=============================================================================
# ENABLE_TIME_STATS 配置（与 Makefile -DTIME_STATS=ON/OFF 一致）
#=============================================================================

if(TIME_STATS)
    add_definitions(-DENABLE_TIME_STATS)
    message(STATUS "ENABLE_TIME_STATS ${_ANSI_ESC}[32mON${_ANSI_ESC}[0m")
else()
    message(STATUS "ENABLE_TIME_STATS ${_ANSI_ESC}[38;5;208mOFF${_ANSI_ESC}[0m")
endif()

#=============================================================================
# 编译器标志配置
#=============================================================================

# 检测 AVX512 支持（仅适用于 Linux 系统）
# 注意：本检测逻辑仅适用于 Linux 系统，不考虑 macOS 和 Windows
# 额外说明：
# - CPU 是否支持 AVX512 与是否可在本项目启用是两件事：
#   还取决于编译器是否支持对应编译选项和 intrinsic。
# - 已查 GCC 官方文档：GCC 4.8.5 的 x86 选项中没有 -mavx512f；
#   GCC 6.1 文档已出现 -mavx512f/相关 AVX512 选项。
# - 结论：GCC 4.8.5 本身不支持 AVX512 编译开关，需升级到更高 GCC 版本（至少 6+）才可评估启用。
# - C++11 本身不是 AVX512 的核心限制因素，关键在编译器前端/后端与头文件支持。
#=============================================================================

# 1. 检查编译器是否支持 AVX512 标志
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512F)
if(COMPILER_SUPPORTS_AVX512F)
    message(STATUS "✅ Compiler supports AVX512F flag")
    set(GCC_SUPPORTS_AVX512 TRUE)
else()
    message(STATUS "❌ Compiler does not support AVX512F flag")
    set(GCC_SUPPORTS_AVX512 FALSE)
endif()

# 2. 检查 CPU 是否支持 AVX512
# 优先方法：读取 /proc/cpuinfo 查找 avx512 相关标志（仅适用于 Linux）
# 仅以 /proc/cpuinfo 是否含 avx512 判定本机 CPU 能力（不做编译测试回退）。
set(CPU_SUPPORTS_AVX512 FALSE)

# 方法1：尝试读取 /proc/cpuinfo（仅适用于 Linux）
if(UNIX AND NOT APPLE)
    file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
    if(CPUINFO_CONTENT)
        # 查找 avx512 相关标志（avx512f, avx512, avx512bw, avx512dq 等）
        string(TOLOWER "${CPUINFO_CONTENT}" CPUINFO_LOWER)
        if(CPUINFO_LOWER MATCHES "avx512")
            message(STATUS "✅ CPU supports AVX512 (detected via /proc/cpuinfo)")
            set(CPU_SUPPORTS_AVX512 TRUE)
        else()
            message(STATUS "❌ CPU does not support AVX512 (checked /proc/cpuinfo)")
        endif()
    else()
        message(STATUS "⚠️  Failed to read /proc/cpuinfo; treat CPU as no AVX512 support")
    endif()
endif()

# 方法2 已移除：仅用「能否编译 AVX-512 源码」不能推断本机 CPU 是否支持 AVX-512；
# 在仅支持 AVX2 的机器上误开 -mavx512f 会在运行时触发 SIGILL（Illegal instruction）。
if(NOT CPU_SUPPORTS_AVX512)
    message(STATUS "❌ CPU does not support AVX512 (no avx512 in /proc/cpuinfo); skip -mavx512f")
endif()

# 3. 综合判断：只有当编译器和 CPU 都支持 AVX512 时才启用
if(GCC_SUPPORTS_AVX512 AND CPU_SUPPORTS_AVX512)
    message(STATUS "✅ CPU and compiler support AVX512 - enabling AVX512 optimizations")
    set(ENABLE_AVX512 TRUE)
else()
    message(STATUS "❌ AVX512 not supported - skipping AVX512 optimizations")
    set(ENABLE_AVX512 FALSE)
endif()

# 4. 命令行 -DDISABLE_AVX512=ON 时强制关闭（不追加 -mavx512f），覆盖上述自动结论
if(DISABLE_AVX512)
    set(ENABLE_AVX512 FALSE)
    message(STATUS "DISABLE_AVX512=ON：强制禁用 AVX-512，不向 Release 追加 -mavx512f")
endif()

# 定义通用编译标志（Debug/Release 共用）
# C++ 语言标准由 CMAKE_CXX_STANDARD / 各目标 CXX_STANDARD 控制，不在此写死 -std=c++11。
# -fPIC：生成位置无关代码，便于链接为 .so（共享库）且降低装载地址限制。
# -fopenmp：启用 OpenMP 并行指令（如 #pragma omp），用于多线程并行计算。
# -msse4.2：启用 SSE4.2 指令集；本项目 CRC/字符串等路径可能用到对应 intrinsic。
#   CRC（Cyclic Redundancy Check，循环冗余校验）常用于快速校验数据完整性。
#   intrinsic（内建函数）可理解为“C/C++ 里直接映射到底层 CPU 指令的函数接口”。
# -fpermissive（历史上曾使用，现已移除）：会把部分语义错误降级为告警，短期提兼容性但会降低类型严格性。
set(COMMON_COMPILE_FLAGS
    -fPIC
    -fopenmp
    -msse4.2
)

# 检测编译器是否支持 -faligned-new（GCC 7+ / 较新 Clang）
# 含 alignas(64) 成员的类型需对齐 new；新编译器 Release -O3 可能生成 vmovdqa，未对齐会 SIGSEGV
check_cxx_compiler_flag("-faligned-new" COMPILER_SUPPORTS_FALIGNED_NEW)
if(COMPILER_SUPPORTS_FALIGNED_NEW)
    list(APPEND COMMON_COMPILE_FLAGS -faligned-new)
    message(STATUS "✅ Compiler supports -faligned-new; enabled for over-aligned new/delete")
else()
    message(STATUS "❌ Compiler does not support -faligned-new; skipped")
endif()

# 定义调试专用标志（在 COMMON_COMPILE_FLAGS 基础上追加）
# -g：写入调试符号，便于 gdb/性能分析工具定位源码与变量。
# -O0：关闭优化，保证执行路径与源码更一致，便于断点和单步排查。
# -Wall：开启常见告警集合，尽早暴露潜在问题。
# -Wextra：补充更多告警规则，覆盖 Wall 未覆盖的边界场景。
# -Wpedantic：对不符合 ISO C++ 标准的写法或编译器扩展发出警告，利于可移植性。
# -Wshadow：对名字遮蔽（内层变量/参数与外层同名）发出警告，减少误用。
# -DDEBUG：定义 DEBUG 宏，允许代码按调试路径启用额外检查/日志。
set(DEBUG_COMPILE_FLAGS
    ${COMMON_COMPILE_FLAGS}
    -g
    -O0
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -DDEBUG
)

# 定义发布专用标志
# ISA = Instruction Set Architecture（指令集架构），CPU 能听懂的“机器指令语法和能力集合”。
# -O3：开启高等级优化，优先提升运行性能（不是编译速度）。
# -march：默认使用 x86-64 基线；当 ENABLE_NATIVE_MARCH=ON 时切换为 native。
# -mavx2：启用 256-bit 向量指令，常提升向量化计算吞吐。
# -mfma：启用融合乘加指令，常提升线性代数/数值计算吞吐并减少中间舍入。
# -mbmi/-mbmi2：位操作扩展（位提取/位排列等），位运算密集路径常有收益。
# -mpopcnt/-mlzcnt：位计数/前导零计数指令，哈希、掩码、索引计算场景常有收益。
# -maes/-mpclmul：AES/无进位乘法指令，加密与部分校验路径可获益（不影响未使用路径）。
# -mtune=generic：做通用微架构调度优化，不额外抬高 ISA 要求。
# -DNDEBUG：由 CMake 默认的 CMAKE_CXX_FLAGS_RELEASE 提供（本列表不重复手写）。
# 旧选项调整说明：
# - 移除 -march=native：避免二进制绑定“构建机 CPU 特性”，降低跨机器部署风险。
# - 移除 -w：不再全局静默告警，避免隐藏潜在正确性/稳定性问题。
# - 不默认启用 -msse4a：SSE4a 为 AMD 扩展，Intel 通常不支持；跨 Intel/AMD 部署存在 SIGILL 风险。
set(RELEASE_COMPILE_FLAGS
    ${COMMON_COMPILE_FLAGS}
    -O3
    -mavx2
    -mfma
    -mbmi
    -mbmi2
    -mpopcnt
    -mlzcnt
    -maes
    -mpclmul
    -mtune=generic
)

if(ENABLE_NATIVE_MARCH)
    set(SELECTED_MARCH_FLAG -march=native)
    message(STATUS "ENABLE_NATIVE_MARCH enabled: use -march=native (Debug/Release)")
else()
    set(SELECTED_MARCH_FLAG -march=x86-64)
    message(STATUS "ENABLE_NATIVE_MARCH disabled: use -march=x86-64 (Debug/Release)")
endif()
list(APPEND RELEASE_COMPILE_FLAGS ${SELECTED_MARCH_FLAG})
list(APPEND DEBUG_COMPILE_FLAGS ${SELECTED_MARCH_FLAG})

# 当且仅当编译器与 CPU 同时支持 AVX512 时自动追加
if(ENABLE_AVX512)
    list(APPEND RELEASE_COMPILE_FLAGS -mavx512f)
    message(STATUS "Added -mavx512f to Release compile flags")
endif()

# 说明：
# 当前项目在 sdp_handler/utils/utils.cpp 中直接使用了 SSE4.2 CRC intrinsic，
# 因此 -msse4.2 放在 COMMON_COMPILE_FLAGS（Debug/Release 都需要）以保证可编译。
# 若后续升级到更新 GCC（例如 >= 8/9）并验证通过，可再评估开启 AVX512 专用构建。
#
# 中/高风险优化选项（当前不启用，仅记录评估要点）：
# 1) -flto（中风险，运行性能导向）
#    - 可能收益：跨翻译单元内联和死代码消除，降低热路径调用开销。
#    - 主要隐患：链接时间明显增加，排障与符号定位复杂度上升，旧工具链兼容性需验证。
# 2) PGO（中风险，运行性能导向）
#    - 可能收益：基于真实负载优化分支预测与代码布局，热点路径通常收益明显。
#    - 主要隐患：构建流程复杂（采样构建 + 训练运行 + 二次构建），
#      训练样本若不代表真实流量，可能导致退化。
# 3) -Ofast / -ffast-math（高风险，运行性能导向）
#    - 可能收益：部分浮点密集代码有额外吞吐提升。
#    - 主要隐患：放宽 IEEE 浮点语义，可能引入数值偏差，不适合直接用于交易关键路径。
# 4) -fno-exceptions / -fno-rtti（高风险，运行性能与体积导向）
#    - 可能收益：减小二进制体积，部分路径减少异常/RTTI 相关开销。
#    - 主要隐患：需要全工程与依赖统一约束，容易引发兼容问题和行为变化。
#
# 结论：上述选项先不采用；后续应在专用分支中，经过压力测试、稳定性测试、回归测试后再评估。

# 应用特定构建类型的标志
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "COMPILE_FLAG: ${DEBUG_COMPILE_FLAGS}")
    add_compile_options(${DEBUG_COMPILE_FLAGS})
    # 将编译标志转换为字符串并定义宏
    string(JOIN " " BUILD_INFO_COMPILE_FLAGS_STR ${DEBUG_COMPILE_FLAGS})
    add_definitions(-DBUILD_INFO_COMPILE_FLAGS="${BUILD_INFO_COMPILE_FLAGS_STR}")
else()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(PEDANTIC_RELEASE_COMPILE_FLAGS
            ${RELEASE_COMPILE_FLAGS}
            -pedantic
        )
        message(STATUS "COMPILE_FLAG: ${PEDANTIC_RELEASE_COMPILE_FLAGS}")
        add_compile_options(${PEDANTIC_RELEASE_COMPILE_FLAGS})
        # 将编译标志转换为字符串并定义宏
        string(JOIN " " BUILD_INFO_COMPILE_FLAGS_STR ${PEDANTIC_RELEASE_COMPILE_FLAGS})
        add_definitions(-DBUILD_INFO_COMPILE_FLAGS="${BUILD_INFO_COMPILE_FLAGS_STR}")
    else()
        message(STATUS "COMPILE_FLAG: ${RELEASE_COMPILE_FLAGS}")
        add_compile_options(${RELEASE_COMPILE_FLAGS})
        # 将编译标志转换为字符串并定义宏
        string(JOIN " " BUILD_INFO_COMPILE_FLAGS_STR ${RELEASE_COMPILE_FLAGS})
        add_definitions(-DBUILD_INFO_COMPILE_FLAGS="${BUILD_INFO_COMPILE_FLAGS_STR}")
    endif()
endif()

# 通用链接器标志
add_link_options(-ldl -pthread)

#=============================================================================
# 通用构建配置
#=============================================================================

# 所有目标的通用头文件目录
set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sdp_handler
)

# 所有目标的通用链接目录
set(COMMON_LINK_DIRS
)

# 所有目标的通用库
# m 为数学库 (math library, 提供如 sin, cos, sqrt, exp 等数学函数，需显式链接)
set(COMMON_LIBS
    m
)

# 包含当前源目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 包含 HDF5 配置（全局配置，所有目标都会继承）
include(${CMAKE_SOURCE_DIR}/cmake/config_hdf5.cmake)
# 调用全局配置函数，达到与之前相同的效果
config_hdf5_global()

# 包含构建信息
# 注意：cmake/build_info.cmake 会自动收集 Git 信息、构建时间、编译器版本等信息
# 并自动添加所有必要的宏定义（BUILD_INFO_BUILD_TIMESTAMP, BUILD_INFO_GIT_REPO_NAME 等）
include(${CMAKE_SOURCE_DIR}/cmake/build_info.cmake)

#=============================================================================
# 开发工具配置
#=============================================================================

# 为 IDE 支持生成编译命令 (VSCode 等)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#=============================================================================
# 子目录
#=============================================================================

# 构建Live应用程序
if(BUILD_APP_LIVE)
    include(${CMAKE_SOURCE_DIR}/cmake/config_zipper.cmake)
    hf_configure_live_zipper()

    add_subdirectory(factors)
    add_subdirectory(models)
    add_subdirectory(app_live)
endif()

# 构建因子本地应用程序
if(BUILD_APP_FACTOR)
    add_subdirectory(factors)
    add_subdirectory(app_factor)
endif()

# 构建模型本地应用程序
if(BUILD_APP_MODEL)
    add_subdirectory(models)
    add_subdirectory(app_model)
endif()

#=============================================================================
# 构建信息
#=============================================================================

message(STATUS "=== HF Stock Cross Section Framework 构建配置 ===")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "策略会话模式(STRATEGY_SESSION_MODE): ${_ANSI_ESC}[32m${STRATEGY_SESSION_MODE}${_ANSI_ESC}[0m")
if(TIME_STATS)
    message(STATUS "TIME_STATS: ${_ANSI_ESC}[32mON${_ANSI_ESC}[0m")
else()
    message(STATUS "TIME_STATS: ${_ANSI_ESC}[38;5;208mOFF${_ANSI_ESC}[0m")
endif()
message(STATUS "C++ 标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "构建目标:")
if (BUILD_APP_LIVE)
    message(STATUS "  - BUILD_APP_LIVE: ${BUILD_APP_LIVE}")
endif()
if (BUILD_APP_FACTOR)
    message(STATUS "  - BUILD_APP_FACTOR: ${BUILD_APP_FACTOR}")
endif()
if (BUILD_APP_MODEL)
    message(STATUS "  - BUILD_APP_MODEL: ${BUILD_APP_MODEL}")
endif()

# 在处理子目录后显示因子库
if (BUILD_APP_FACTOR OR BUILD_APP_LIVE)
    get_property(FACTOR_LIBS GLOBAL PROPERTY FACTOR_LIBS)
    if(FACTOR_LIBS)
        message(STATUS "因子库: ${FACTOR_LIBS}")
    endif()
endif()

# 在处理子目录后显示模型库
if (BUILD_APP_MODEL OR BUILD_APP_LIVE)
    get_property(MODEL_LIBS GLOBAL PROPERTY MODEL_LIBS)
    if(MODEL_LIBS)
        message(STATUS "模型库: ${MODEL_LIBS}")
    endif()
endif()

message(STATUS "HDF5 配置:")
message(STATUS "  - 根目录: ${HDF5_ROOT}")
message(STATUS "  - 头文件: ${HDF5_INCLUDE_DIR}")
message(STATUS "  - 库目录: ${HDF5_LIBRARY_DIR}")
