# factors/CMakeLists.txt
# 自动发现并 add_subdirectory 各因子模块（目录名不以 _ 开头），汇总为 factors_comm 与 generated/factors_include.h。
# 单因子构建逻辑在 FactorModuleTemplate.cmake；各目录 CMakeLists 见 factors/_template/CMakeLists.txt。

include(${CMAKE_SOURCE_DIR}/cmake/ModuleCxxStandard.cmake)

# 自动发现所有因子集目录（示例：demo0000、democs00；排除 _comm、_template、_share）
file(GLOB FACTOR_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*")
set(FACTOR_LIBS "")
set(FACTOR_INCLUDES "")

# factors_comm：公共 .cc（如因子注册列表展示），供 app_live / app_factor 链接。
# C++ 标准始终跟随顶层 CMAKE_CXX_STANDARD（显式设置，不随单因子目录的 MODULE_CXX_STANDARD 覆盖）。
add_library(factors_comm STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/_comm/factor_display.cc
)

# factors_share：带实现的共享工具（per1day 数据读取、hdf5_utils 等），依赖 HDF5；
# 需用的因子在自身 CMakeLists「可选 C」链接 factors_share（与 app_* 隔离，不反向依赖）。
add_library(factors_share STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/_share/per1day_data_getter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/_share/hdf5_utils.cc
)

hf_apply_project_cxx_standard(factors_comm)
hf_apply_project_cxx_standard(factors_share)

# 设置 factors_comm 的包含目录
target_include_directories(factors_comm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# 设置 factors_share 的包含目录
target_include_directories(factors_share PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# HDF5：本目录显式 include，便于 factors/ 单独作为子工程配置时仍能解析 configure_hdf5（顶层已 include 时重复 include 无害）
include(${CMAKE_SOURCE_DIR}/cmake/config_hdf5.cmake)
configure_hdf5(factors_share)

# 刻意不在此对 factors_comm 写 target_link_libraries(factors_share …)：
# share 仅由具体因子在各自 CMakeLists 里按需 PRIVATE 链接，避免 factors_comm / app 强依赖 HDF5。

# 添加 factors/_comm 目录的头文件为全局可见
# - 作用范围：factors/ 及其 add_subdirectory 包含的所有因子 target（如 factors_xxx 静态库等），不影响factors/以外的其他目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_comm)

# 添加 factors/_share 目录的头文件为全局可见
# - 作用范围：factors/ 及其 add_subdirectory 包含的所有因子 target（如 factors_xxx 静态库等），不影响factors/以外的其他目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_share)

# 处理每个因子目录
foreach(DIR ${FACTOR_DIRS})
    # 检查是否是目录且不是 _ 开头（如_comm、_template、_share等都会被过滤）
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${DIR} AND
        NOT DIR MATCHES "^_.*" AND
        NOT DIR STREQUAL "tests")
        message(STATUS "Found factor module: ${DIR}")

        # 添加子目录
        add_subdirectory(${DIR})

        # 添加到因子库列表
        list(APPEND FACTOR_LIBS "factors_${DIR}")

        # 🔥 添加到包含列表（用于生成 factors_include.h）
        list(APPEND FACTOR_INCLUDES "#include \"factors/${DIR}/factor_entry.h\"")

        # factors_comm → factors_<DIR> 用 PUBLIC：对外只暴露 factors_comm，但 app 链接时必须连带各已注册因子静态库
        # （静态注册符号、factor_entry 实现等）。PUBLIC 将链接依赖传给链接 factors_comm 的上游（app_*）；
        # 若改为 PRIVATE，app 只链 factors_comm 时不会自动带上 factors_<DIR>，易产生未定义符号。
        target_link_libraries(factors_comm PUBLIC "factors_${DIR}")
    endif()
endforeach()

# 自动生成 factors_include.h（供 app 编译期 include 各因子 factor_entry.h，触发静态注册）
# 示例生成内容：#include "factors/demo0000/factor_entry.h"
set(GENERATED_FACTORS_H ${CMAKE_BINARY_DIR}/generated/factors_include.h)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

# 生成头文件内容
set(FACTORS_H_CONTENT "#pragma once\n\n// 🔥 自动生成的因子包含文件 - 请勿手动编辑\n\n")
foreach(INCLUDE_LINE ${FACTOR_INCLUDES})
    set(FACTORS_H_CONTENT "${FACTORS_H_CONTENT}${INCLUDE_LINE}\n")
endforeach()

# 写入生成的头文件
file(WRITE ${GENERATED_FACTORS_H} "${FACTORS_H_CONTENT}")
message(STATUS "✅ Generated factors_include.h at: ${GENERATED_FACTORS_H}")

# 打印发现的因子库
message(STATUS "Factor libraries: ${FACTOR_LIBS}")

# 将因子库列表设置为全局属性以便其他地方使用
set_property(GLOBAL PROPERTY FACTOR_LIBS "${FACTOR_LIBS}")
