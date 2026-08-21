# 从源码编静态库 raw_data_reader_core；父工程 add_subdirectory 后链接 raw_data_reader::core。
# 将本目录作为子工程接入其它 CMake 项目：
#   find_package(Threads REQUIRED)
#   add_subdirectory(path/to/raw_data_reader)
#   target_link_libraries(你的可执行文件 PRIVATE raw_data_reader::core)
#
# 你的源码里应一律以 raw_data_reader 目录为 include 根：
#   #include "ticks_data.h"
#   #include "raw_data_types/my_stock.h"
# 不再单独把 raw_data_types 加到 -I，避免裸写 "my_*.h"；路径由本目标 PUBLIC 传递。
#
# 默认值与命令行覆盖（与 raw_data_reader/Makefile 中 READER_BACKEND 语义一致）：
#   - 默认 RAW_DATA_READER_BACKEND=cpp；可选 cpp、python、both。
#   - C++ 标准：作为其它工程的 add_subdirectory 时，与顶层 CMAKE_CXX_STANDARD 一致；
#     仅当本目录为 CMake 顶层源码目录（独立配置 raw_data_reader）时，才使用缓存 RAW_DATA_READER_CXX_STANDARD（默认 11）。
#   - 配置阶段传入：cmake -DRAW_DATA_READER_BACKEND=cpp ... 或 ccmake 改缓存。
# 产物目录：由父工程的 CMAKE_BINARY_DIR / 目标 OUTPUT_DIRECTORY 决定；Makefile 侧对应变量为 BUILD_DIR_REL（默认 build，相对仓库根）。

cmake_minimum_required(VERSION 3.14)
project(raw_data_reader LANGUAGES CXX)

set(RAW_DATA_READER_BACKEND "cpp" CACHE STRING "READER_BACKEND: cpp | python | both（默认 cpp；与 Makefile READER_BACKEND 对齐）")
set_property(CACHE RAW_DATA_READER_BACKEND PROPERTY STRINGS cpp python both)

# 独立构建本目录：用缓存指定标准；
# 作为子目录：与父工程 CMAKE_CXX_STANDARD 一致（不写全局 CMAKE_CXX_STANDARD，避免影响主工程其它目标）
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(RAW_DATA_READER_CXX_STANDARD "11" CACHE STRING "C++ standard for raw_data_reader_core（仅独立 -S 本目录时生效）")
	set_property(CACHE RAW_DATA_READER_CXX_STANDARD PROPERTY STRINGS 11 14 17 20)
	set(_rdr_cxx_standard ${RAW_DATA_READER_CXX_STANDARD})
else()
	if(CMAKE_CXX_STANDARD)
		set(_rdr_cxx_standard "${CMAKE_CXX_STANDARD}")
	else()
		set(_rdr_cxx_standard 11)
	endif()
endif()

find_package(Threads REQUIRED)

set(_rdr_root "${CMAKE_CURRENT_SOURCE_DIR}")

# 所有后端共用的 .cc；python/both 再追加 IO 与嵌入相关源文件（与 Makefile 列表一致）
set(_rdr_sources
	merge_sort.cc
	data_path_config.cc
	ticks_data.cc
)

# ---------- 按 RAW_DATA_READER_BACKEND 追加源文件、定义宏、必要时拉取 Python3.8 + numpy 头路径 ----------
if(RAW_DATA_READER_BACKEND STREQUAL "cpp")
	list(APPEND _rdr_sources ticks_data_io_cpp.cc)
	set(_rdr_compile_def READER_BACKEND_CPP_ONLY)
elseif(RAW_DATA_READER_BACKEND STREQUAL "python")
	list(APPEND _rdr_sources
		ticks_data_io_python.cc
		my_data.cc
	)
	find_package(Python3 3.8 COMPONENTS Interpreter Development Development.Embed REQUIRED)
	if(NOT Python3_VERSION MATCHES "^3\\.8\\.")
		message(FATAL_ERROR "需要 CPython 3.8.x 以嵌入链接（RAW_DATA_READER_BACKEND=python），当前为 ${Python3_VERSION}。请设置 Python3_ROOT_DIR 指向 python3.8 安装前缀（见 README.md）。")
	endif()
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" -c "import numpy; print(numpy.get_include())"
		OUTPUT_VARIABLE _numpy_inc
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _numpy_rc
	)
	if(NOT _numpy_rc EQUAL 0)
		message(FATAL_ERROR "需要 Python3.8 + numpy（RAW_DATA_READER_BACKEND=python）")
	endif()
	set(_rdr_compile_def READER_BACKEND_PYTHON_ONLY)
	set(_rdr_need_python TRUE)
elseif(RAW_DATA_READER_BACKEND STREQUAL "both")
	list(APPEND _rdr_sources
		ticks_data_io_cpp.cc
		ticks_data_io_python.cc
		my_data.cc
	)
	find_package(Python3 3.8 COMPONENTS Interpreter Development Development.Embed REQUIRED)
	if(NOT Python3_VERSION MATCHES "^3\\.8\\.")
		message(FATAL_ERROR "需要 CPython 3.8.x 以嵌入链接（RAW_DATA_READER_BACKEND=both），当前为 ${Python3_VERSION}。请设置 Python3_ROOT_DIR 指向 python3.8 安装前缀（见 README.md）。")
	endif()
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" -c "import numpy; print(numpy.get_include())"
		OUTPUT_VARIABLE _numpy_inc
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _numpy_rc
	)
	if(NOT _numpy_rc EQUAL 0)
		message(FATAL_ERROR "需要 Python3.8 + numpy（RAW_DATA_READER_BACKEND=both）")
	endif()
	set(_rdr_compile_def READER_BACKEND_BOTH)
	set(_rdr_need_python TRUE)
else()
	message(FATAL_ERROR "RAW_DATA_READER_BACKEND 必须是 cpp、python 或 both")
endif()

# ---------- 配置摘要（编译模式与本库关键参数，配置阶段打印一次）----------
if(CMAKE_CONFIGURATION_TYPES)
	set(_rdr_build_type_line "多配置生成器（Visual Studio / Xcode 等），构建时用 --config Release|Debug|...")
else()
	if(CMAKE_BUILD_TYPE)
		set(_rdr_build_type_line "${CMAKE_BUILD_TYPE}")
	else()
		set(_rdr_build_type_line "未设置（建议在配置时加 -DCMAKE_BUILD_TYPE=Release 或 Debug）")
	endif()
endif()
message(STATUS "")
message(STATUS "[raw_data_reader] ========== configure summary ==========")
message(STATUS "[raw_data_reader] 编译模式 (CMAKE_BUILD_TYPE)     : ${_rdr_build_type_line}")
message(STATUS "[raw_data_reader] RAW_DATA_READER_BACKEND        : ${RAW_DATA_READER_BACKEND}")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(STATUS "[raw_data_reader] C++ 标准（RAW_DATA_READER_CXX_STANDARD）: ${_rdr_cxx_standard}")
else()
	message(STATUS "[raw_data_reader] C++ 标准（继承主工程 CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}）: ${_rdr_cxx_standard}")
endif()
message(STATUS "[raw_data_reader] READER_BACKEND 宏               : ${_rdr_compile_def}")
message(STATUS "[raw_data_reader] CMAKE_CXX_COMPILER_ID          : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "[raw_data_reader] CMAKE_CXX_COMPILER_VERSION      : ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "[raw_data_reader] raw_data_reader_core PRIVATE 选项: -Wall -g -pthread -fPIC")
message(STATUS "[raw_data_reader] 源码根 (include 根)             : ${_rdr_root}")
message(STATUS "[raw_data_reader] =========================================")
message(STATUS "")

# 静态库目标：PUBLIC 传递 include 根为 raw_data_reader 目录，与「头文件写 raw_data_types/」约定一致
add_library(raw_data_reader_core STATIC ${_rdr_sources})

set_target_properties(raw_data_reader_core PROPERTIES
	CXX_STANDARD ${_rdr_cxx_standard}
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

target_compile_definitions(raw_data_reader_core PUBLIC "${_rdr_compile_def}")

target_include_directories(raw_data_reader_core PUBLIC "${_rdr_root}")

target_compile_options(raw_data_reader_core PRIVATE -Wall -g -pthread -fPIC)

# 仅 python/both：把 Python 与 numpy 头目录挂到 PUBLIC，依赖方自动获得 -I
if(_rdr_need_python)
	target_include_directories(raw_data_reader_core PUBLIC
		${Python3_INCLUDE_DIRS}
		"${_numpy_inc}"
	)
	target_link_libraries(raw_data_reader_core PUBLIC Python3::Python Threads::Threads)
else()
	target_link_libraries(raw_data_reader_core PUBLIC Threads::Threads)
endif()

add_library(raw_data_reader::core ALIAS raw_data_reader_core)
