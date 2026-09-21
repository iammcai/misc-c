# 添加可执行文件
add_executable(Core
    core/core.c
)

# 头文件目录
target_link_libraries(Core PUBLIC
    Mgnt
    Lib
    Net
    App
    Test
)

target_link_options(Core PRIVATE
    "LINKER:--whole-archive"
    $<TARGET_FILE:Mgnt>
    $<TARGET_FILE:Lib>
    $<TARGET_FILE:Net>
    "LINKER:--no-whole-archive"
)