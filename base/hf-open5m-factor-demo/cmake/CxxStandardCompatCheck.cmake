#=============================================================================
# C++ 标准与 CMake / 编译器版本兼容性检查（黄色 STATUS 提示，不中断配置）
# configure 彩色 STATUS（ANSI；重定向到文件时可能显示转义码）：
#   蓝色 [34] — 工程穿透标准与子模块本地标准不一致（ModuleCxxStandard.cmake）
#   黄色 [33] — 本文件工具链兼容性告警
#=============================================================================
if(NOT COMMAND hf_message_status_blue)
	function(hf_message_status_colored color_code message_text)
		string(ASCII 27 Esc)
		set(_c_reset "${Esc}[m")
		message(STATUS "${Esc}[${color_code}m${message_text}${_c_reset}")
	endfunction()

	function(hf_message_status_blue message_text)
		hf_message_status_colored(34 "${message_text}")
	endfunction()

	function(hf_message_status_yellow message_text)
		hf_message_status_colored(33 "${message_text}")
	endfunction()
endif()

if(_HF_CXX_STANDARD_COMPAT_CHECK_INCLUDED)
	return()
endif()
set(_HF_CXX_STANDARD_COMPAT_CHECK_INCLUDED TRUE)
# 版本对照与根 CMakeLists.txt、ModuleCxxStandard.cmake 注释一致。
# 配置阶段：由根 CMakeLists.txt include 并检查 CMAKE_CXX_STANDARD；模块本地标准见 ModuleCxxStandard.cmake。
# 可选手动：cmake -DCMAKE_CXX_STANDARD=17 -P cmake/CxxStandardCompatCheck.cmake（不经过 project，编译器探测与正式配置可能略有差异）。

# 各 C++ 标准所需最低 CMake 版本（CXX_STANDARD 属性支持）
set(_HF_CXX_CMAKE_MIN_11 "3.1")
set(_HF_CXX_CMAKE_MIN_14 "3.2")
set(_HF_CXX_CMAKE_MIN_17 "3.8")
set(_HF_CXX_CMAKE_MIN_20 "3.12")

# 各 C++ 标准所需最低 g++ 版本
set(_HF_CXX_GXX_MIN_11 "4.8.1")
set(_HF_CXX_GXX_MIN_14 "5.1")
set(_HF_CXX_GXX_MIN_17 "7.1")
set(_HF_CXX_GXX_MIN_20 "10.1")

# 建议 g++ 版本（低于此时额外提示，仍允许继续）
set(_HF_CXX_GXX_REC_17 "8.0")
set(_HF_CXX_GXX_REC_20 "11.0")

# 各 C++ 标准所需最低 Clang 主版本（非 Apple 时按 CMAKE_CXX_COMPILER_VERSION）
set(_HF_CXX_CLANG_MIN_11 "3.3")
set(_HF_CXX_CLANG_MIN_14 "3.4")
set(_HF_CXX_CLANG_MIN_17 "5.0")
set(_HF_CXX_CLANG_MIN_20 "10.0")

function(_hf_compat_status_yellow message_text)
	hf_message_status_yellow("${message_text}")
endfunction()

function(hf_warn_cxx_standard_toolchain_compat_impl cxx_standard compiler_id compiler_version cmake_version)
	if("${cxx_standard}" STREQUAL "")
		set(cxx_standard "11")
	endif()

	if(NOT cxx_standard MATCHES "^[0-9]+$")
		set(_msg "")
		string(APPEND _msg
			"⚠️ [C++标准兼容性] CMAKE_CXX_STANDARD=\"${cxx_standard}\" 不是本检查支持的整数标准（11/14/17/20 等）；"
			"请自行确认工具链，配置仍继续。"
		)
		_hf_compat_status_yellow("${_msg}")
		return()
	endif()

	set(_cmake_min_var "_HF_CXX_CMAKE_MIN_${cxx_standard}")
	if(DEFINED ${_cmake_min_var})
		set(_need_cmake "${${_cmake_min_var}}")
		if("${cmake_version}" VERSION_LESS "${_need_cmake}")
			set(_msg "")
			string(APPEND _msg
				"⚠️ [C++标准兼容性] C++${cxx_standard} 建议 CMake ≥ ${_need_cmake}，当前 CMake ${cmake_version}；"
				"可能无法正确设置 -std=c++${cxx_standard}，配置仍继续。"
			)
			_hf_compat_status_yellow("${_msg}")
		endif()
	else()
		set(_msg "")
		string(APPEND _msg
			"⚠️ [C++标准兼容性] C++${cxx_standard} 未列入本工程 CMake 最低版本表（当前仅维护 11/14/17/20）；"
			"请自行确认 CMake ${cmake_version} 是否支持，配置仍继续。"
		)
		_hf_compat_status_yellow("${_msg}")
	endif()

	if("${compiler_id}" STREQUAL "")
		set(_msg "")
		string(APPEND _msg
			"⚠️ [C++标准兼容性] 未能识别 C++ 编译器，跳过与 C++${cxx_standard} 的编译器版本校验；配置仍继续。"
		)
		_hf_compat_status_yellow("${_msg}")
		return()
	endif()

	if(compiler_id STREQUAL "GNU")
		set(_gxx_min_var "_HF_CXX_GXX_MIN_${cxx_standard}")
		if(DEFINED ${_gxx_min_var})
			set(_need_gxx "${${_gxx_min_var}}")
			if("${compiler_version}" VERSION_LESS "${_need_gxx}")
				set(_msg "")
				string(APPEND _msg
					"⚠️ [C++标准兼容性] C++${cxx_standard} 需要 g++ ≥ ${_need_gxx}，"
					"当前 g++ ${compiler_version}；编译可能失败或语言特性不完整，配置仍继续。"
				)
				_hf_compat_status_yellow("${_msg}")
			endif()
			if(cxx_standard EQUAL 17)
				set(_rec "${_HF_CXX_GXX_REC_17}")
				if(NOT compiler_version VERSION_LESS "${_need_gxx}"
					AND compiler_version VERSION_LESS "${_rec}")
					set(_msg "")
					string(APPEND _msg
						"⚠️ [C++标准兼容性] C++17 建议使用 g++ ≥ ${_rec}，当前 g++ ${compiler_version}；配置仍继续。"
					)
					_hf_compat_status_yellow("${_msg}")
				endif()
			elseif(cxx_standard EQUAL 20)
				set(_rec "${_HF_CXX_GXX_REC_20}")
				if(NOT compiler_version VERSION_LESS "${_need_gxx}"
					AND compiler_version VERSION_LESS "${_rec}")
					set(_msg "")
					string(APPEND _msg
						"⚠️ [C++标准兼容性] C++20 建议使用 g++ ≥ ${_rec}，当前 g++ ${compiler_version}；配置仍继续。"
					)
					_hf_compat_status_yellow("${_msg}")
				endif()
			endif()
		else()
			set(_msg "")
			string(APPEND _msg
				"⚠️ [C++标准兼容性] C++${cxx_standard} 未列入本工程 g++ 最低版本表；"
				"当前 g++ ${compiler_version}，请自行确认，配置仍继续。"
			)
			_hf_compat_status_yellow("${_msg}")
		endif()
	elseif(compiler_id MATCHES "Clang")
		set(_clang_min_var "_HF_CXX_CLANG_MIN_${cxx_standard}")
		if(DEFINED ${_clang_min_var})
			set(_need_clang "${${_clang_min_var}}")
			if("${compiler_version}" VERSION_LESS "${_need_clang}")
				set(_msg "")
				string(APPEND _msg
					"⚠️ [C++标准兼容性] C++${cxx_standard} 需要 Clang ≥ ${_need_clang}，"
					"当前 ${compiler_id} ${compiler_version}；编译可能失败，配置仍继续。"
				)
				_hf_compat_status_yellow("${_msg}")
			endif()
		else()
			set(_msg "")
			string(APPEND _msg
				"⚠️ [C++标准兼容性] C++${cxx_standard} 未列入本工程 Clang 最低版本表；"
				"当前 ${compiler_id} ${compiler_version}，请自行确认，配置仍继续。"
			)
			_hf_compat_status_yellow("${_msg}")
		endif()
	else()
		set(_msg "")
		string(APPEND _msg
			"⚠️ [C++标准兼容性] 编译器 ${compiler_id} ${compiler_version} 无内置版本对照表；"
			"请自行确认是否支持 C++${cxx_standard}，配置仍继续。"
		)
		_hf_compat_status_yellow("${_msg}")
	endif()
endfunction()

function(hf_warn_cxx_standard_toolchain_compat cxx_standard)
	hf_warn_cxx_standard_toolchain_compat_impl(
		"${cxx_standard}"
		"${CMAKE_CXX_COMPILER_ID}"
		"${CMAKE_CXX_COMPILER_VERSION}"
		"${CMAKE_VERSION}"
	)
endfunction()

# 供 cmake -P 调用：根据环境 CXX 或 g++ 探测编译器后检查（与配置阶段结论一致）
function(_hf_detect_cxx_compiler_for_script out_id out_version)
	if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
		set(_cxx "$ENV{CXX}")
	else()
		set(_cxx "g++")
	endif()
	execute_process(
		COMMAND ${_cxx} --version
		OUTPUT_VARIABLE _version_line
		ERROR_VARIABLE _version_err
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _rc
	)
	if(NOT _rc EQUAL 0)
		set(${out_id} "" PARENT_SCOPE)
		set(${out_version} "" PARENT_SCOPE)
		return()
	endif()
	if(_version_line MATCHES [Cc]lang)
		set(_id "Clang")
	else()
		set(_id "GNU")
	endif()
	execute_process(
		COMMAND ${_cxx} -dumpversion
		OUTPUT_VARIABLE _ver
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
		RESULT_VARIABLE _dump_rc
	)
	if(_dump_rc EQUAL 0 AND NOT _ver STREQUAL "")
		set(${out_version} "${_ver}" PARENT_SCOPE)
	else()
		if(_version_line MATCHES "[Vv]ersion[ ]+([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
			set(${out_version} "${CMAKE_MATCH_1}" PARENT_SCOPE)
		else()
			set(${out_version} "" PARENT_SCOPE)
		endif()
	endif()
	set(${out_id} "${_id}" PARENT_SCOPE)
endfunction()

# cmake -P 入口（Makefile 在正式 configure 前调用）
if(CMAKE_SCRIPT_MODE_FILE)
	if(NOT DEFINED CMAKE_CXX_STANDARD OR "${CMAKE_CXX_STANDARD}" STREQUAL "")
		set(CMAKE_CXX_STANDARD "11")
	endif()
	_hf_detect_cxx_compiler_for_script(_script_cxx_id _script_cxx_ver)
	hf_warn_cxx_standard_toolchain_compat_impl(
		"${CMAKE_CXX_STANDARD}"
		"${_script_cxx_id}"
		"${_script_cxx_ver}"
		"${CMAKE_VERSION}"
	)
endif()
