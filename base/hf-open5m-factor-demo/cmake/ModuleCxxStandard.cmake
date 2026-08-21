#=============================================================================
# 因子/模型模块 C++ 标准：穿透 CMAKE_CXX_STANDARD 与本地覆盖
#=============================================================================
# app_live / app_factor / app_model 主目标不在此文件设置标准：其 add_library/add_executable 未设
# CXX_STANDARD，由根 CMakeLists.txt 的 CMAKE_CXX_STANDARD 经 CMake 目录树默认值穿透（见该处注释）。
# 本文件中的函数仅用于需要显式 CXX_STANDARD 的因子/模型静态库及 comm/share 库。
#
# 关于「cxx_std_14」与「-std=c++14」（勿对同一目标叠两套）：
#   - -std=c++14：g++/clang 命令行开关，直接指定语言标准。
#   - cxx_std_14：CMake 编译特性名；是否可用取决于 CMake 探测到的编译器能力表
#     （CMAKE_CXX_COMPILE_FEATURES），与「本机 g++ 能否编 C++14」并不总是一致。
#   - CXX_STANDARD 目标属性：由 CMake 按编译器生成等价 -std= / /std: 等（推荐、统一入口）。
# 本文件对 STATIC/SHARED 与（CMake ≥ 3.15 的）INTERFACE 均只设 CXX_STANDARD；
# 仅当 CMake < 3.15 且目标为 INTERFACE 时，用 INTERFACE 的 -std=c++N 传播（该版本无 INTERFACE CXX_STANDARD）。
#
include(${CMAKE_SOURCE_DIR}/cmake/CxxStandardCompatCheck.cmake)
# 各 C++ 标准所需最低 g++ 版本（与根 CMakeLists.txt 一致；混用模块标准时取各模块要求的最高值）：
#   C++11 → g++ ≥ 4.8.1
#   C++14 → g++ ≥ 5.1
#   C++17 → g++ ≥ 7.1（建议 ≥ 8）
#   C++20 → g++ ≥ 10.1（建议 ≥ 11）
#
# 在 include FactorModuleTemplate.cmake / ModelModuleTemplate.cmake 之前可设置（因子与模型共用变量名）：
#
#   set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
#   set(MODULE_CXX_STANDARD 14)              # USE_PROJECT=OFF 时必填
#
# 每个因子/模型目录是独立 add_subdirectory 作用域，同名变量不会在 demo0000 与 demo_simple 之间串台。
# 未设置 USE_PROJECT 时默认为 ON（跟随顶层 CMAKE_CXX_STANDARD）。
#
# 模块专有编译选项（如 -fpermissive）请在各子模块 CMakeLists.txt 中、include 模板之后对 ${LIB_NAME}
# 使用 target_compile_options / target_compile_definitions，参见 factors|models/_template/CMakeLists.txt。
#
# configure 提示颜色（见 CxxStandardCompatCheck.cmake 头部）：
#   蓝色 — 子模块 MODULE_CXX_STANDARD 与工程穿透 CMAKE_CXX_STANDARD 不一致；
#   黄色 — 该标准与 CMake / g++ / Clang 版本可能不兼容。

# 对任意目标写入 C++ 标准（单一机制，不按编译器能力表分支 cxx_std_*）
function(_hf_set_target_cxx_standard target cxx_std)
	get_target_property(_target_type ${target} TYPE)
	if(_target_type STREQUAL "INTERFACE_LIBRARY" AND CMAKE_VERSION VERSION_LESS "3.15")
		# CMake 3.10–3.14：INTERFACE 尚无 CXX_STANDARD，用 INTERFACE -std= 向链接方传播
		target_compile_options(${target} INTERFACE
			$<$<COMPILE_LANGUAGE:CXX>:-std=c++${cxx_std}>
		)
	else()
		set_target_properties(${target} PROPERTIES
			CXX_STANDARD "${cxx_std}"
			CXX_STANDARD_REQUIRED ON
			CXX_EXTENSIONS OFF
		)
	endif()
endfunction()

function(hf_apply_module_cxx_standard target)
	if(NOT TARGET ${target})
		return()
	endif()

	if(NOT DEFINED MODULE_USE_PROJECT_CXX_STANDARD)
		set(MODULE_USE_PROJECT_CXX_STANDARD ON)
	endif()

	set(_project_cxx "${CMAKE_CXX_STANDARD}")
	if("${_project_cxx}" STREQUAL "")
		set(_project_cxx "11")
	endif()

	if(MODULE_USE_PROJECT_CXX_STANDARD)
		set(_effective_cxx "${_project_cxx}")
		set(_source_desc "穿透 CMAKE_CXX_STANDARD")
	else()
		if(NOT DEFINED MODULE_CXX_STANDARD OR "${MODULE_CXX_STANDARD}" STREQUAL "")
			message(FATAL_ERROR
				"[${target}] MODULE_USE_PROJECT_CXX_STANDARD=OFF 时必须设置 MODULE_CXX_STANDARD（例如 11、14、17）。"
			)
		endif()
		set(_effective_cxx "${MODULE_CXX_STANDARD}")
		set(_source_desc "模块本地 MODULE_CXX_STANDARD")
	endif()

	_hf_set_target_cxx_standard(${target} "${_effective_cxx}")

	if(NOT MODULE_USE_PROJECT_CXX_STANDARD)
		if(NOT _effective_cxx STREQUAL _project_cxx)
			set(_mismatch_msg "")
			string(APPEND _mismatch_msg
				"ℹ️  [${target}] C++标准: 本模块使用 C++${_effective_cxx}（${_source_desc}），"
				"与工程穿透 C++${_project_cxx}（CMAKE_CXX_STANDARD）不一致；混链时请确认 ABI 与内联边界。"
			)
			hf_message_status_blue("${_mismatch_msg}")
		endif()
	endif()

	get_target_property(_target_type ${target} TYPE)
	if(_target_type STREQUAL "INTERFACE_LIBRARY" AND CMAKE_VERSION VERSION_LESS "3.15")
		set(_mech_desc "INTERFACE -std=c++${_effective_cxx}（CMake < 3.15）")
	else()
		set(_mech_desc "CXX_STANDARD")
	endif()
	message(STATUS "✅ [${target}] C++标准: C++${_effective_cxx}（${_source_desc}；${_mech_desc}）")

	# 本地覆盖标准时检查工具链；与工程一致时由根 CMakeLists 对 CMAKE_CXX_STANDARD 检查
	if(NOT MODULE_USE_PROJECT_CXX_STANDARD OR NOT _effective_cxx STREQUAL _project_cxx)
		hf_warn_cxx_standard_toolchain_compat("${_effective_cxx}")
	endif()
endfunction()

function(hf_apply_project_cxx_standard target)
	if(NOT TARGET ${target})
		return()
	endif()
	set(_project_cxx "${CMAKE_CXX_STANDARD}")
	if("${_project_cxx}" STREQUAL "")
		set(_project_cxx "11")
	endif()
	_hf_set_target_cxx_standard(${target} "${_project_cxx}")
endfunction()
