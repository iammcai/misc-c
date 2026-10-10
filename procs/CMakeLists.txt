# 添加可执行文件
add_executable(Core
    core/core.c
)

add_library(Proc STATIC
    proc/proc.c
    rpc/rpc.c
    proc/heart_beat.c
    test_proc/test.c
)

# 头文件目录
target_include_directories(Proc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Proc PUBLIC
    Mgnt
    Lib
    Net
    App
    Test
)

target_link_libraries(Core PUBLIC
    Proc
)

target_link_options(Proc PRIVATE
    "LINKER:--whole-archive"
    $<TARGET_FILE:Mgnt>
    $<TARGET_FILE:Lib>
    $<TARGET_FILE:Net>
    "LINKER:--no-whole-archive"
)