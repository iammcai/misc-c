cmake_minimum_required(VERSION 3.10)
project(MISC-C)

set(CMAKE_BUILD_TYPE Debug)

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
    -Wl,--whole-archive
    Mgnt
    -Wl,--no-whole-archive
    -Wl,--whole-archive
    Lib
    -Wl,--no-whole-archive
    -Wl,--whole-archive
    Net
    -Wl,--no-whole-archive
    Test
)

# 控制asan启用，默认关闭
option(ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)

if(ENABLE_ASAN)
    message(STATUS "[ASAN] AddressSanitizer enabled")
    # 添加编译选项
    add_compile_options(
        -fsanitize=address
        -fno-omit-frame-pointer
        -g                      # ASan 必须配合调试符号才能输出可读调用栈
        -O1                     # 推荐：纯 -O0 下 ASan 极慢，-O1 是最佳平衡点
    )
    # 链接
    add_link_options(-fsanitize=address)
else()
    message(STATUS "[ASAN] AddressSanitizer disabled")
endif()