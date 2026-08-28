cmake_minimum_required(VERSION 3.10)
project(MISC-C)

add_compile_definitions(_GNU_SOURCE)

# set(CMAKE_BUILD_TYPE Debug)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 控制asan启用，默认关闭
option(ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)
if(ENABLE_ASAN)
    message(STATUS "[ASAN] AddressSanitizer enabled")
    # 推荐 Debug 或 RelWithDebInfo，纯 Release 下 ASAN 符号信息差
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING "[ASAN] Release mode detected, switching to RelWithDebInfo for better diagnostics")
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()

    # 全局生效：覆盖所有后续 add_subdirectory 中的目标
    add_compile_options(
        -fsanitize=address
        -fno-omit-frame-pointer
        -g              # 确保有调试符号
        -O1             # ASan 官方推荐 O1，兼顾速度与检测能力
    )
    add_link_options(-fsanitize=address)

    # 可选：启用更严格的检测
    # add_compile_options(-fsanitize-address-use-after-scope)
else()
    message(STATUS "[ASAN] AddressSanitizer disabled")
endif()

# 添加可执行文件
add_executable(Main
    main.c
)

# 子目录
add_subdirectory(lib)
add_subdirectory(mgnt)
add_subdirectory(net)
add_subdirectory(app)
add_subdirectory(test)
add_subdirectory(driver)

# 头文件目录
target_link_libraries(Main PUBLIC
    Mgnt
    Lib
    Net
    App
    Test
)

target_link_options(Main PRIVATE
    "LINKER:--whole-archive"
    $<TARGET_FILE:Mgnt>
    $<TARGET_FILE:Lib>
    $<TARGET_FILE:Net>
    "LINKER:--no-whole-archive"
)