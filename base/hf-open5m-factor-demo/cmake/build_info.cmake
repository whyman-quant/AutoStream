# 构建信息模块（项目根/cmake/build_info.cmake）
# 顶层 CMakeLists.txt：include(${CMAKE_SOURCE_DIR}/cmake/build_info.cmake)
# 注意：此模块会自动通过 add_definitions() 添加所有必要的宏定义，无需手动处理

# =============================================================================
# Git 信息
# =============================================================================
# 注意：所有临时变量都使用 BUILD_INFO_ 前缀以避免与项目中的变量冲突

# 获取 Git 仓库根目录，并据此提取仓库名（目录名）
execute_process(
    COMMAND git rev-parse --show-toplevel
    OUTPUT_VARIABLE BUILD_INFO_GIT_TOPLEVEL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(BUILD_INFO_GIT_TOPLEVEL)
    get_filename_component(BUILD_INFO_GIT_REPO_NAME ${BUILD_INFO_GIT_TOPLEVEL} NAME)
else()
    set(BUILD_INFO_GIT_REPO_NAME "unknown")
endif()

# 获取 Git 当前分支名
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE BUILD_INFO_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT BUILD_INFO_GIT_BRANCH)
    set(BUILD_INFO_GIT_BRANCH "unknown")
endif()

# 获取 Git 当前短提交哈希
execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE BUILD_INFO_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT BUILD_INFO_GIT_COMMIT)
    set(BUILD_INFO_GIT_COMMIT "unknown")
endif()

# 获取 Git 工作区是否干净（clean/dirty）
execute_process(
    COMMAND git status --porcelain
    OUTPUT_VARIABLE BUILD_INFO_GIT_STATUS_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(BUILD_INFO_GIT_STATUS_OUTPUT STREQUAL "")
    set(BUILD_INFO_GIT_STATUS "clean")
else()
    set(BUILD_INFO_GIT_STATUS "dirty")
endif()

# =============================================================================
# 构建与工具链信息
# =============================================================================

# 获取构建时间戳（配置阶段时间）
string(TIMESTAMP BUILD_INFO_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# 获取 CMake 版本（只保留 --version 首行）
execute_process(
    COMMAND ${CMAKE_COMMAND} --version
    OUTPUT_VARIABLE BUILD_INFO_CMAKE_VERSION_FULL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "\n.*" "" BUILD_INFO_CMAKE_VERSION "${BUILD_INFO_CMAKE_VERSION_FULL}")
if(NOT BUILD_INFO_CMAKE_VERSION)
    set(BUILD_INFO_CMAKE_VERSION "unknown")
endif()

# 获取 Make 版本（只保留 --version 首行）
execute_process(
    COMMAND ${CMAKE_MAKE_PROGRAM} --version
    OUTPUT_VARIABLE BUILD_INFO_MAKE_VERSION_FULL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "\n.*" "" BUILD_INFO_MAKE_VERSION "${BUILD_INFO_MAKE_VERSION_FULL}")
if(NOT BUILD_INFO_MAKE_VERSION)
    set(BUILD_INFO_MAKE_VERSION "unknown")
endif()

# 获取 g++ 版本（命令行工具版本；只保留首行）
execute_process(
    COMMAND g++ --version
    OUTPUT_VARIABLE BUILD_INFO_GXX_CMD_VERSION_FULL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "\n.*" "" BUILD_INFO_GXX_CMD_VERSION "${BUILD_INFO_GXX_CMD_VERSION_FULL}")
if(NOT BUILD_INFO_GXX_CMD_VERSION)
    set(BUILD_INFO_GXX_CMD_VERSION "unknown")
endif()

# 获取 gcc 版本（命令行工具版本；只保留首行）
execute_process(
    COMMAND gcc --version
    OUTPUT_VARIABLE BUILD_INFO_GCC_CMD_VERSION_FULL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "\n.*" "" BUILD_INFO_GCC_CMD_VERSION "${BUILD_INFO_GCC_CMD_VERSION_FULL}")
if(NOT BUILD_INFO_GCC_CMD_VERSION)
    set(BUILD_INFO_GCC_CMD_VERSION "unknown")
endif()

# 获取 C++ 编译器版本（来自 CMAKE_CXX_COMPILER；只保留首行）
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} --version
    OUTPUT_VARIABLE BUILD_INFO_COMPILER_VERSION_FULL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

string(REGEX REPLACE "\n.*" "" BUILD_INFO_CXX_COMPILER_VERSION "${BUILD_INFO_COMPILER_VERSION_FULL}")

# 获取 C++ 标准版本：
# 1) 优先使用 CMAKE_CXX_STANDARD；
# 2) 若未设置，再从 CMAKE_CXX_FLAGS 中提取 -std=...；
# 3) 仍未命中则标记为 unknown。
if(CMAKE_CXX_STANDARD)
    set(BUILD_INFO_CXX_STANDARD "C++${CMAKE_CXX_STANDARD}")
else()
    # 如果没有设置，尝试从 CMAKE_CXX_FLAGS 中提取
    # 查找 -std=c++XX 或 -std=gnu++XX 模式
    string(REGEX MATCH "-std=(c\\+\\+|gnu\\+\\+)([0-9]+)" BUILD_INFO_CXX_STD_MATCH "${CMAKE_CXX_FLAGS}")
    if(BUILD_INFO_CXX_STD_MATCH)
        string(REGEX REPLACE ".*-std=(c\\+\\+|gnu\\+\\+)([0-9]+).*" "C++\\2" BUILD_INFO_CXX_STANDARD "${CMAKE_CXX_FLAGS}")
    else()
        # 如果都找不到，设置为 unknown
        set(BUILD_INFO_CXX_STANDARD "unknown")
    endif()
endif()

# 获取 CPU 型号（Linux: /proc/cpuinfo；无结果时回退 unknown）
execute_process(
    COMMAND sh -c "grep -m 1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' || echo 'unknown'"
    OUTPUT_VARIABLE BUILD_INFO_CPU_MODEL_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(BUILD_INFO_CPU_MODEL_RAW)
    set(BUILD_INFO_CPU_MODEL "${BUILD_INFO_CPU_MODEL_RAW}")
else()
    set(BUILD_INFO_CPU_MODEL "unknown")
endif()

# =============================================================================
# ONNX Runtime / CUDA / cuDNN（编译配置期探测）
# =============================================================================

if(DEFINED ONNX_RUNTIME AND NOT ONNX_RUNTIME STREQUAL "")
    set(BUILD_INFO_ONNX_RUNTIME "${ONNX_RUNTIME}")
else()
    set(BUILD_INFO_ONNX_RUNTIME "none")
endif()

set(BUILD_INFO_CUDA_VERSION "unknown")
set(_BUILD_INFO_NVCC "nvcc")
if(DEFINED ENV{CUDA_HOME} AND EXISTS "$ENV{CUDA_HOME}/bin/nvcc")
    set(_BUILD_INFO_NVCC "$ENV{CUDA_HOME}/bin/nvcc")
elseif(EXISTS "/usr/local/cuda/bin/nvcc")
    set(_BUILD_INFO_NVCC "/usr/local/cuda/bin/nvcc")
endif()
execute_process(
    COMMAND ${_BUILD_INFO_NVCC} --version
    OUTPUT_VARIABLE _BUILD_INFO_NVCC_OUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(_BUILD_INFO_NVCC_OUT MATCHES "release ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
    set(BUILD_INFO_CUDA_VERSION "${CMAKE_MATCH_1}")
endif()

set(BUILD_INFO_CUDNN_VERSION "unknown")
set(_BUILD_INFO_CUDNN_HEADERS "")
if(DEFINED ENV{CUDA_HOME})
    list(APPEND _BUILD_INFO_CUDNN_HEADERS "$ENV{CUDA_HOME}/include/cudnn_version.h")
endif()
list(APPEND _BUILD_INFO_CUDNN_HEADERS
    "/usr/local/cuda/include/cudnn_version.h"
    "/usr/include/cudnn_version.h"
)
foreach(_BUILD_INFO_CUDNN_HDR IN LISTS _BUILD_INFO_CUDNN_HEADERS)
    if(EXISTS "${_BUILD_INFO_CUDNN_HDR}")
        file(READ "${_BUILD_INFO_CUDNN_HDR}" _BUILD_INFO_CUDNN_H_CONTENT)
        set(_BUILD_INFO_CUDNN_MAJOR "")
        set(_BUILD_INFO_CUDNN_MINOR "0")
        set(_BUILD_INFO_CUDNN_PATCH "0")
        if(_BUILD_INFO_CUDNN_H_CONTENT MATCHES "#define CUDNN_MAJOR[ \t]+([0-9]+)")
            set(_BUILD_INFO_CUDNN_MAJOR "${CMAKE_MATCH_1}")
        endif()
        if(_BUILD_INFO_CUDNN_H_CONTENT MATCHES "#define CUDNN_MINOR[ \t]+([0-9]+)")
            set(_BUILD_INFO_CUDNN_MINOR "${CMAKE_MATCH_1}")
        endif()
        if(_BUILD_INFO_CUDNN_H_CONTENT MATCHES "#define CUDNN_PATCHLEVEL[ \t]+([0-9]+)")
            set(_BUILD_INFO_CUDNN_PATCH "${CMAKE_MATCH_1}")
        endif()
        if(_BUILD_INFO_CUDNN_MAJOR)
            set(BUILD_INFO_CUDNN_VERSION "${_BUILD_INFO_CUDNN_MAJOR}.${_BUILD_INFO_CUDNN_MINOR}.${_BUILD_INFO_CUDNN_PATCH}")
        endif()
        break()
    endif()
endforeach()

# =============================================================================
# 宏导出与调试输出
# =============================================================================

# 导出为预处理宏（供 C/C++ 代码读取）
add_definitions(
    -DBUILD_INFO_BUILD_TIMESTAMP="${BUILD_INFO_BUILD_TIMESTAMP}"
    -DBUILD_INFO_GIT_REPO_NAME="${BUILD_INFO_GIT_REPO_NAME}"
    -DBUILD_INFO_GIT_BRANCH="${BUILD_INFO_GIT_BRANCH}"
    -DBUILD_INFO_GIT_COMMIT="${BUILD_INFO_GIT_COMMIT}"
    -DBUILD_INFO_GIT_STATUS="${BUILD_INFO_GIT_STATUS}"
    -DBUILD_INFO_CMAKE_VERSION="${BUILD_INFO_CMAKE_VERSION}"
    -DBUILD_INFO_MAKE_VERSION="${BUILD_INFO_MAKE_VERSION}"
    -DBUILD_INFO_GXX_CMD_VERSION="${BUILD_INFO_GXX_CMD_VERSION}"
    -DBUILD_INFO_GCC_CMD_VERSION="${BUILD_INFO_GCC_CMD_VERSION}"
    -DBUILD_INFO_CXX_COMPILER_VERSION="${BUILD_INFO_CXX_COMPILER_VERSION}"
    -DBUILD_INFO_CXX_STANDARD="${BUILD_INFO_CXX_STANDARD}"
    -DBUILD_INFO_CPU_MODEL="${BUILD_INFO_CPU_MODEL}"
    -DBUILD_INFO_ONNX_RUNTIME="${BUILD_INFO_ONNX_RUNTIME}"
    -DBUILD_INFO_CUDA_VERSION="${BUILD_INFO_CUDA_VERSION}"
    -DBUILD_INFO_CUDNN_VERSION="${BUILD_INFO_CUDNN_VERSION}"
)

# 在 CMake 配置阶段打印（可选，用于调试）
message(STATUS "构建时间: ${BUILD_INFO_BUILD_TIMESTAMP}")
message(STATUS "Git仓库: ${BUILD_INFO_GIT_REPO_NAME}")
message(STATUS "Git分支: ${BUILD_INFO_GIT_BRANCH}")
message(STATUS "Git提交: ${BUILD_INFO_GIT_COMMIT}")
message(STATUS "Git状态: ${BUILD_INFO_GIT_STATUS}")
message(STATUS "CMake版本: ${BUILD_INFO_CMAKE_VERSION}")
message(STATUS "Make版本: ${BUILD_INFO_MAKE_VERSION}")
message(STATUS "g++版本: ${BUILD_INFO_GXX_CMD_VERSION}")
message(STATUS "gcc版本: ${BUILD_INFO_GCC_CMD_VERSION}")
message(STATUS "C++编译器版本: ${BUILD_INFO_CXX_COMPILER_VERSION}")
message(STATUS "C++标准: ${BUILD_INFO_CXX_STANDARD}")
message(STATUS "CPU型号: ${BUILD_INFO_CPU_MODEL}")
message(STATUS "ONNX Runtime(编译): ${BUILD_INFO_ONNX_RUNTIME}")
message(STATUS "CUDA(编译探测): ${BUILD_INFO_CUDA_VERSION}")
message(STATUS "cuDNN(编译探测): ${BUILD_INFO_CUDNN_VERSION}")
