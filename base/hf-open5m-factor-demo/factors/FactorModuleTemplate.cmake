#=============================================================================
# 通用因子模块 CMake 模板（由 factors/<因子目录名>/CMakeLists.txt include）
#=============================================================================
# 最小用法示例（新建因子目录 my_factor 时，CMakeLists.txt 通常只需一行）：
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)
#
# 更完整写法（可选 A/B/C）见 factors/_template/CMakeLists.txt；复制 _template 改名为普通目录后按需删改。
#
# 本模板行为摘要：
#   - 目标名：factors_<当前目录名>，例如目录 demo0000 → 静态库 factors_demo0000，变量 ${LIB_NAME} 同此名。
#   - 源文件：本目录及子目录下 *.cpp / *.cc；若无源文件则生成 INTERFACE 库（仅导出头文件路径）。
#   - C++ 标准：默认 hf_apply_module_cxx_standard 跟随顶层 CMAKE_CXX_STANDARD
#     （示例：make build CMAKE_CXX_STANDARD=17 或 make build-factor CMAKE_CXX_STANDARD=14）。
#   - 本地覆盖标准（与工程不一致时配置阶段会 ⚠️）：在 include 本文件「之前」写，例如：
#       set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
#       set(MODULE_CXX_STANDARD 14)
#   - 标准与 g++/CMake 是否匹配：cmake/CxxStandardCompatCheck.cmake（仅 WARNING，不中断配置）。
#
# 可选配置（写在「各因子目录」的 CMakeLists.txt，不要写在本模板里）：
#   可选 A（include 之前）：MODULE_USE_PROJECT_CXX_STANDARD / MODULE_CXX_STANDARD（与 models 共用，见 ModuleCxxStandard.cmake）
#   可选 B（include 之后）：target_compile_options(${LIB_NAME} PRIVATE …)，如 -fpermissive、-Wno-pedantic
#   可选 C（include 之后）：target_link_libraries(${LIB_NAME} PRIVATE factors_share)
#     PRIVATE 原因：share 仅本因子使用，不向 factors_comm / app 传递 HDF5 等依赖

include(${CMAKE_SOURCE_DIR}/cmake/ModuleCxxStandard.cmake)

# 当前子目录名即因子集名（与 factors/<name>/ 中 <name> 一致）
get_filename_component(FACTOR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(LIB_NAME "factors_${FACTOR_NAME}")

# 收集本模块实现源文件（factor_entry.cc、其它 .cc/.cpp 等）
file(GLOB_RECURSE SOURCES "*.cpp" "*.cc")

# 本因子库对外暴露的头文件搜索路径（PUBLIC：链接 factors_<name> 的目标也能 include）
set(INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/factors/_comm
    ${CMAKE_SOURCE_DIR}/factors/_share
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external
)

if(SOURCES)
    add_library(${LIB_NAME} STATIC ${SOURCES})
    target_include_directories(${LIB_NAME} PUBLIC ${INCLUDE_DIRS})

    # 静态库供 app_live 等链接为 .so 的一部分，需 -fPIC
    set_target_properties(${LIB_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
else()
    # 仅头文件、无 .cc/.cpp 时生成 INTERFACE 库（实现全在 .h 内，如 demo0001）
    add_library(${LIB_NAME} INTERFACE)
    target_include_directories(${LIB_NAME} INTERFACE ${INCLUDE_DIRS})
endif()

# 有/无源文件均应用 MODULE_* 与 configure 阶段提示（无源文件时用 INTERFACE + cxx_std_* 传播）
hf_apply_module_cxx_standard(${LIB_NAME})

message(STATUS "✅ Configured factor module: ${FACTOR_NAME} (library: ${LIB_NAME})")
