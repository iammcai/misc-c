# 添加源文件
add_library(Lib STATIC
    type/type_list.c
    type/type_hash.c
    type/type_atom_queue.c
    event/ev_thread.c
    mp/mp.c
)

# 添加头文件路径
target_include_directories(Lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)