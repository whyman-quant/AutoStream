# 本目录为脚手架：CMake 父级 factors/CMakeLists.txt 不会收录以 _ 开头的子目录。
# 复制到普通目录名后，本文件一般只需保留 include；FactorModuleTemplate.cmake 会按目录名生成
# factors_<目录名> 静态库，并自动收集本目录下 *.cpp / *.cc。

# =============================================================================
# 可选 A：本因子模块固定 C++ 标准（须在 include 模板「之前」取消注释）
# =============================================================================
# 默认跟随顶层 make build 传入的 CMAKE_CXX_STANDARD（如 make build CMAKE_CXX_STANDARD=17）。
# 仅当本因子需要与工程其它部分使用不同标准时，再启用下面两行：
#
# g++ 最低版本（与 cmake/ModuleCxxStandard.cmake 一致）：
#   MODULE_CXX_STANDARD 11 → g++ ≥ 4.8.1
#   MODULE_CXX_STANDARD 14 → g++ ≥ 5.1
#   MODULE_CXX_STANDARD 17 → g++ ≥ 7.1（建议 ≥ 8）
#   MODULE_CXX_STANDARD 20 → g++ ≥ 10.1（建议 ≥ 11）
#
# set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
# set(MODULE_CXX_STANDARD 14)
#
# 若本地标准与工程穿透不一致，CMake 配置阶段会输出 ⚠️ 提示。
# 工具链检查：cmake/CxxStandardCompatCheck.cmake（例：MODULE_CXX_STANDARD 17 需 g++ ≥ 7.1，仅 WARNING）。

# =============================================================================
# 必选：引入因子模块模板（生成 ${LIB_NAME} 静态库；下面可选 B/C 依赖此行）
# =============================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

# =============================================================================
# 可选 B：本因子模块专有编译选项（须在 include 模板「之后」取消注释）
# =============================================================================
# 下面写法只作用于本目录生成的 ${LIB_NAME}，不会改到其它因子/模型或 app_live。
# 示例（按需取消注释，可写多行）：
#
# -fpermissive（GCC）：将部分本应为错误的 C++ 语义问题降级为警告，便于兼容较宽松或老旧写法；会降低类型检查严格性，仅在本模块确有需求时启用。
# target_compile_options(${LIB_NAME} PRIVATE -fpermissive)
# -Wno-pedantic：在本目标上关闭 -pedantic 产生的「偏离 ISO C++」类告警（如第三方头里的宏展开）；不关闭其它警告，也不影响工程全局 -pedantic。
# target_compile_options(${LIB_NAME} PRIVATE -Wno-pedantic)

# =============================================================================
# 可选 C：链接 factors_share（不需要则保持注释）
# =============================================================================
# factors_share 提供：per1day_data_getter、factors::hdf5_utils（HDF5 读写，见 factors/_share/hdf5_utils.h）。
# 示例：demo0000、demosy00 使用 per1day 或 hdf5_utils 时取消下一行注释；纯头文件因子无需链接。
# factors_<DIR> → factors_share 用 PRIVATE：share 仅为本因子实现细节，不向 factors_comm / app 传播 HDF5 等
# usage；未用 share 的因子保持本行注释，避免整仓强依赖 factors_share。不用 INTERFACE：本 target 自身要链入 .a。
# target_link_libraries(${LIB_NAME} PRIVATE factors_share)
