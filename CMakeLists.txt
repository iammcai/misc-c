cmake_minimum_required(VERSION 3.10)
project(MISC-C)

# 添加可执行文件
add_executable(Main
    main.c
)

# 子目录
add_subdirectory(lib)
add_subdirectory(mgnt)
add_subdirectory(zcap)
add_subdirectory(test)

# 头文件目录
target_link_libraries(Main PUBLIC
    Mgnt
    Lib
    Zcap
    Test
)