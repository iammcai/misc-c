cmake_minimum_required(VERSION 3.10)
project(MISC-C)

# set(CMAKE_BUILD_TYPE Debug)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 添加可执行文件
add_executable(Main
    main.c
)

# 子目录
add_subdirectory(lib)
add_subdirectory(mgnt)
add_subdirectory(net)
add_subdirectory(test)

# 头文件目录
target_link_libraries(Main PUBLIC
    Mgnt
    Lib
    Net
    Test
)

# 控制asan启用，默认关闭
option(ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)

if(ENABLE_ASAN)
    message(STATUS "[ASAN] AddressSanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
else()
    message(STATUS "[ASAN] AddressSanitizer disabled")
endif()