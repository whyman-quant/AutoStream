# =============================================================================
# [已弃用] 项目根目录 config_hdf5.cmake
#
# 历史上 HDF5 配置曾放在框架根目录；现已迁至 cmake/config_hdf5.cmake。
# 本文件仅为尚未改动的旧因子 / 模型 CMakeLists 提供兼容，后续将删除。
#
# 框架顶层请使用：
#   include(${CMAKE_SOURCE_DIR}/cmake/config_hdf5.cmake)
#   config_hdf5_global()
#
# 单目标（旧写法，不推荐）请改用顶层全局配置，勿再 include 本文件。
# =============================================================================

function(warn_deprecated_root_config_hdf5 usage_hint)
    message(WARNING
        "[Deprecated] 正在通过项目根目录的 config_hdf5.cmake ${usage_hint}；"
        "该路径即将移除。请改用："
        "include(${CMAKE_SOURCE_DIR}/cmake/config_hdf5.cmake)"
    )
endfunction()

# 全局配置 HDF5（所有目标都会继承）
function(config_hdf5_global)
    warn_deprecated_root_config_hdf5("调用 config_hdf5_global()")

    set(HDF5_ROOT "/mnt/beegfs_ssd_raid91/706_wgh_new/wgh_team_share/software/my_new_hdf5" CACHE PATH "HDF5 安装根目录")
    set(HDF5_INCLUDE_DIR "${HDF5_ROOT}/include" CACHE PATH "HDF5 头文件目录")
    set(HDF5_LIBRARY_DIR "${HDF5_ROOT}/lib" CACHE PATH "HDF5 库目录")
    set(HDF5_LIBRARIES
        "${HDF5_LIBRARY_DIR}/libhdf5_hl.a"
        "${HDF5_LIBRARY_DIR}/libhdf5.a"
        "${HDF5_LIBRARY_DIR}/libszip.a"
        "${HDF5_LIBRARY_DIR}/libz.a"
    )

    include_directories(${HDF5_INCLUDE_DIR})
    link_libraries(${HDF5_LIBRARIES})

    message(STATUS "HDF5 已全局配置")
    message(STATUS "HDF5 根目录: ${HDF5_ROOT}")
    message(STATUS "HDF5 头文件目录: ${HDF5_INCLUDE_DIR}")
    message(STATUS "HDF5 库目录: ${HDF5_LIBRARY_DIR}")
endfunction()

# 针对特定目标配置 HDF5（旧因子 / 模型 CMakeLists 常见写法）
function(configure_hdf5 target_name)
    warn_deprecated_root_config_hdf5("对目标 '${target_name}' 调用 configure_hdf5()")

    set(HDF5_ROOT_LOCAL "/mnt/beegfs_ssd_raid91/706_wgh_new/wgh_team_share/software/my_new_hdf5")
    set(HDF5_INCLUDE_DIR_LOCAL "${HDF5_ROOT_LOCAL}/include")
    set(HDF5_LIBRARY_DIR_LOCAL "${HDF5_ROOT_LOCAL}/lib")
    set(HDF5_LIBRARIES
        "${HDF5_LIBRARY_DIR_LOCAL}/libhdf5_hl.a"
        "${HDF5_LIBRARY_DIR_LOCAL}/libhdf5.a"
        "${HDF5_LIBRARY_DIR_LOCAL}/libszip.a"
        "${HDF5_LIBRARY_DIR_LOCAL}/libz.a"
    )

    target_include_directories(${target_name} PRIVATE ${HDF5_INCLUDE_DIR_LOCAL})
    target_link_libraries(${target_name} PRIVATE ${HDF5_LIBRARIES})

    message(STATUS "HDF5 已配置到目标: ${target_name}")
endfunction()
