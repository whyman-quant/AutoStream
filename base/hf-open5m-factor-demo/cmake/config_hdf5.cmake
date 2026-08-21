# HDF5 全局/按目标配置（项目根/cmake/config_hdf5.cmake）
# 顶层 CMakeLists.txt：include(${CMAKE_SOURCE_DIR}/cmake/config_hdf5.cmake)

# 全局配置 HDF5（所有目标都会继承）
function(config_hdf5_global)
    # 定义 HDF5 路径（CACHE 变量，全局可访问）
    set(HDF5_ROOT "/mnt/beegfs_ssd_raid91/706_wgh_new/wgh_team_share/software/my_new_hdf5" CACHE PATH "HDF5 安装根目录")
    set(HDF5_INCLUDE_DIR "${HDF5_ROOT}/include" CACHE PATH "HDF5 头文件目录")
    set(HDF5_LIBRARY_DIR "${HDF5_ROOT}/lib" CACHE PATH "HDF5 库目录")
    # 按正确顺序定义 HDF5 库
    set(HDF5_LIBRARIES
        "${HDF5_LIBRARY_DIR}/libhdf5_hl.a"
        "${HDF5_LIBRARY_DIR}/libhdf5.a"
        "${HDF5_LIBRARY_DIR}/libszip.a"      # szip 压缩
        "${HDF5_LIBRARY_DIR}/libz.a"         # zlib 压缩
    )

    # 全局配置 HDF5（所有目标都会继承）
    # 说明：仅注入头文件与绝对路径库，不注入 link_directories，
    # 避免将 HDF5 lib 目录带入运行时搜索候选而引发系统路径里比如 /usr/lib64/libz.so.1 冲突警告。
    include_directories(${HDF5_INCLUDE_DIR})
    # 历史写法（保留原语句供参考）：
    # link_directories(${HDF5_LIBRARY_DIR})
    link_libraries(${HDF5_LIBRARIES})

    message(STATUS "HDF5 已全局配置")
    message(STATUS "HDF5 根目录: ${HDF5_ROOT}")
    message(STATUS "HDF5 头文件目录: ${HDF5_INCLUDE_DIR}")
    message(STATUS "HDF5 库目录: ${HDF5_LIBRARY_DIR}")
endfunction()

# 针对特定目标配置 HDF5（用于兼容老版本因子）
function(configure_hdf5 target_name)
    # 定义 HDF5 路径（局部变量，只在函数内部有效）
    set(HDF5_ROOT_LOCAL "/mnt/beegfs_ssd_raid91/706_wgh_new/wgh_team_share/software/my_new_hdf5")
    set(HDF5_INCLUDE_DIR_LOCAL "${HDF5_ROOT_LOCAL}/include")
    set(HDF5_LIBRARY_DIR_LOCAL "${HDF5_ROOT_LOCAL}/lib")
    # 按正确顺序定义 HDF5 库
    # 下面这个 set(HDF5_LIBRARIES ...) 只在该函数作用域有效，
    # 调用完成后，HDF5_LIBRARIES 在函数外部不可见
    set(HDF5_LIBRARIES
        "${HDF5_LIBRARY_DIR_LOCAL}/libhdf5_hl.a"
        "${HDF5_LIBRARY_DIR_LOCAL}/libhdf5.a"
        "${HDF5_LIBRARY_DIR_LOCAL}/libszip.a"      # szip 压缩
        "${HDF5_LIBRARY_DIR_LOCAL}/libz.a"         # zlib 压缩
    )

    # 配置目标
    target_include_directories(${target_name} PRIVATE ${HDF5_INCLUDE_DIR_LOCAL})
    # 说明：HDF5_LIBRARIES 使用绝对路径，target_link_directories 非必需，移除以减少 RPATH 干扰。
    # 历史写法（保留原语句供参考）：
    # target_link_directories(${target_name} PRIVATE ${HDF5_LIBRARY_DIR_LOCAL})
    target_link_libraries(${target_name} PRIVATE ${HDF5_LIBRARIES})

    message(STATUS "HDF5 已配置到目标: ${target_name}")
endfunction()
