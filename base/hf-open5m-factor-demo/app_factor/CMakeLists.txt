#=============================================================================
# 项目配置
#=============================================================================
# app_factor：入口为 main，纯本地读行情→算因子→落 HDF5。
# 与 app_live 的差异：（1）无 sdp_handler / 模型引擎链路；（2）运行时语义上等同 local_simulate。
# 策略会话模式 OPEN/CLOSE/CONTINUOUS 由顶层 STRATEGY_SESSION_MODE 与 factor 引擎宏对齐 app_live。
#=============================================================================

# 定义项目名称
set(PROJECT_NAME_EN local-factor)
project(${PROJECT_NAME_EN})

# 定义可执行文件目标名称
set(EXECUTABLE_TARGET_NAME main)

find_package(Threads REQUIRED)

# 逐笔原始数据读取子工程（独立 CMakeLists，产出 raw_data_reader::core）
add_subdirectory(comm/raw_data_reader)

#=============================================================================
# 目标定义
#=============================================================================

# 定义源文件
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/comm/general.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/comm/hdf5_utils.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/comm/tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/config_parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/data/data_loader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/factor_calculation_engine.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
)

# 创建可执行文件目标
# C++ 标准：未设 CXX_STANDARD，跟随根 CMakeLists.txt 的 CMAKE_CXX_STANDARD（CMake 目录树默认穿透）。
add_executable(${EXECUTABLE_TARGET_NAME} ${SOURCES})

#=============================================================================
# 目标配置
#=============================================================================

# 设置头文件目录
target_include_directories(${EXECUTABLE_TARGET_NAME} PRIVATE
    ${COMMON_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/comm
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/thread
    # 🔥 包含生成的头文件目录
    ${CMAKE_BINARY_DIR}/generated
)


# 设置链接目录
target_link_directories(${EXECUTABLE_TARGET_NAME} PRIVATE
    ${COMMON_LINK_DIRS}
)

# 链接库
# app → factors_comm 用 PRIVATE：main 为最终可执行文件，消费 factors_comm 及其 PUBLIC 传递的各 factors_<DIR>；
# app 不向其它 target 再传递因子库依赖，故不用 PUBLIC。不直接链 factors_share（由具体因子按需 PRIVATE 链接）。
target_link_libraries(${EXECUTABLE_TARGET_NAME} PRIVATE
    ${COMMON_LIBS}
    factors_comm
    raw_data_reader::core
    Threads::Threads
)

#=============================================================================
# 构建信息
#=============================================================================

message(STATUS "=== ${PROJECT_NAME_EN} 应用配置 ===")
message(STATUS "目标: ${EXECUTABLE_TARGET_NAME} (可执行文件)")
message(STATUS "源文件: ${SOURCES}")