#=============================================================================
# app_live：Zipper 与系统 zlib 集成（项目根/cmake/config_zipper.cmake）
#=============================================================================
# 顶层 CMakeLists.txt 在 BUILD_APP_LIVE 分支内调用：
#   include(${CMAKE_SOURCE_DIR}/cmake/config_zipper.cmake)
#   hf_configure_live_zipper()
#
# 职责：
#   1) hf_strategy_system_zlib：strategy 等目标使用的系统 libz（不经 FindZLIB，避免与 zipper 纠缠）
#   2) add_subdirectory(app_live/comm/zipper) 及 staticZipper 编译选项
#   3) 内置 zlib 时 staticZipper 仅登记 libz.a，消除与系统 libz 的 RPATH 冲突警告

function(hf_configure_live_zipper)
    # -------------------------------------------------------------------------
    # 与 zipper 子目录的 zlib 解耦（须写在 add_subdirectory(zipper) 之前）
    #
    # 背景：zipper 自带 CMakeLists 会 find_package(ZLIB)，必要时在子目录二进制树里编一份 zlib，
    #       并占用全局的 ZLIB_* 缓存与 ZLIB::ZLIB 导入目标。若 app_live 也用 find_package(ZLIB)
    #       或 ZLIB::ZLIB，容易与树内 zipper 实际使用的 zlib 路径纠缠。
    # 做法：本工程里「非 zipper」的 strategy 等目标不经过 FindZLIB；在此处单独解析「系统/
    #       当前环境」下的 zlib 头与库文件路径，并挂到一个专用导入目标上，供 app_live 链接。
    #       zipper 仍完全按其自带脚本处理自己的 zlib，二者路径可以不同，互不影响。
    # -------------------------------------------------------------------------

    find_path(HF_STRATEGY_ZLIB_INCLUDE_DIR "zlib.h" REQUIRED)

    find_library(
        HF_STRATEGY_ZLIB_LIBRARY
        NAMES z zlib zdll zlib1 zlibstatic zlibstat
        PATHS
            /usr/lib64
            /usr/lib
            /lib64
            /lib
            /usr/lib/x86_64-linux-gnu
        ENV LIBRARY_PATH
        NO_CMAKE_FIND_ROOT_PATH
        REQUIRED
    )

    if(NOT TARGET hf_strategy_system_zlib)
        add_library(hf_strategy_system_zlib UNKNOWN IMPORTED)
        set_target_properties(
            hf_strategy_system_zlib
            PROPERTIES
                IMPORTED_LOCATION "${HF_STRATEGY_ZLIB_LIBRARY}"
        )
        # 系统默认头路径（如 /usr/include）不要写入 INTERFACE_INCLUDE_DIRECTORIES：
        # CMake 对导入目标会当作 -isystem 传播，会破坏 cstdlib 的 #include_next 链。
        if(NOT HF_STRATEGY_ZLIB_INCLUDE_DIR MATCHES "^(/usr|/usr/local)/include$")
            set_target_properties(
                hf_strategy_system_zlib
                PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${HF_STRATEGY_ZLIB_INCLUDE_DIR}"
            )
        endif()
    endif()
    message(STATUS "hf_strategy_system_zlib: ${HF_STRATEGY_ZLIB_LIBRARY} ; ${HF_STRATEGY_ZLIB_INCLUDE_DIR}")

    # 使用原装 zipper CMake 编译静态库，并关闭其测试构建
    set(BUILD_TEST OFF CACHE BOOL "Disable zipper tests from parent project" FORCE)
    set(BUILD_SHARED_VERSION OFF CACHE BOOL "Disable shared zipper library from parent project" FORCE)
    set(BUILD_STATIC_VERSION ON CACHE BOOL "Enable static zipper library from parent project" FORCE)
    add_subdirectory(app_live/comm/zipper)

    # 仅对 zipper 子项目的 C 源文件关闭告警，避免第三方 minizip 在当前全局编译选项下刷屏。
    foreach(_zipper_target IN ITEMS staticZipper Zipper-static)
        if(TARGET ${_zipper_target})
            # zipper 子工程固定 C++11，不跟随顶层 CMAKE_CXX_STANDARD
            set_target_properties(${_zipper_target} PROPERTIES
                CXX_STANDARD 11
                CXX_STANDARD_REQUIRED ON
                CXX_EXTENSIONS OFF
            )
            target_compile_options(${_zipper_target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-w>
                $<$<COMPILE_LANGUAGE:C>:-Wno-complain-wrong-lang>
            )
        endif()
    endforeach()

    # zipper 内置 zlib 时，其 CMake 会把 libz.a 与 libz.so 一并登记到 staticZipper 的链接依赖；
    # strategy 再链 hf_strategy_system_zlib（系统 libz.so.1）时，CMake 生成 RPATH 会警告两路 libz 冲突。
    # 不改第三方 zipper 源文件：将 static 目标上的 zlib 收成仅 libz.a，避免向 strategy 传递 zlib_install 下的 .so。
    set(_HF_ZIPPER_BUNDLED_ZLIB_A "${CMAKE_BINARY_DIR}/app_live/comm/zipper/zlib_install/lib/libz.a")
    if(EXISTS "${_HF_ZIPPER_BUNDLED_ZLIB_A}")
        foreach(_zipper_static_target IN ITEMS staticZipper Zipper-static)
            if(NOT TARGET ${_zipper_static_target})
                continue()
            endif()
            foreach(_zipper_link_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(_zipper_libs ${_zipper_static_target} ${_zipper_link_prop})
                if(NOT _zipper_libs OR _zipper_libs STREQUAL "${_zipper_link_prop}-NOTFOUND")
                    continue()
                endif()
                set(_zipper_libs_new "")
                set(_zipper_has_zlib_a FALSE)
                foreach(_zipper_lib_entry IN LISTS _zipper_libs)
                    if(_zipper_lib_entry MATCHES "\\$<")
                        list(APPEND _zipper_libs_new "${_zipper_lib_entry}")
                        continue()
                    endif()
                    if(_zipper_lib_entry MATCHES "libz\\.so" OR _zipper_lib_entry MATCHES "/libz\\.so\\.")
                        continue()
                    endif()
                    if(_zipper_lib_entry MATCHES "libz\\.a$" OR _zipper_lib_entry MATCHES "/libz\\.a"
                        OR _zipper_lib_entry MATCHES "zlib_install")
                        if(NOT _zipper_has_zlib_a)
                            list(APPEND _zipper_libs_new "${_HF_ZIPPER_BUNDLED_ZLIB_A}")
                            set(_zipper_has_zlib_a TRUE)
                        endif()
                        continue()
                    endif()
                    list(APPEND _zipper_libs_new "${_zipper_lib_entry}")
                endforeach()
                if(NOT _zipper_has_zlib_a)
                    list(APPEND _zipper_libs_new "${_HF_ZIPPER_BUNDLED_ZLIB_A}")
                endif()
                set_target_properties(${_zipper_static_target} PROPERTIES ${_zipper_link_prop} "${_zipper_libs_new}")
            endforeach()
        endforeach()
        message(STATUS "zipper 静态库 zlib 依赖已收成仅 libz.a: ${_HF_ZIPPER_BUNDLED_ZLIB_A}")
    endif()
endfunction()
