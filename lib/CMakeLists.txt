# 添加源文件
add_library(Lib STATIC
    type/type_list.c
    type/type_hash.c
    type/type_atom_queue.c
    type/type_heap.c
    type/type_skiplist.c
    event/ev_thread.c
    mp/mp.c
    event/ev_lock.c
    msg_q/msg_q.c
    plat/debug.c
)

# 添加链接库
target_link_libraries(Lib
    atomic
    Mgnt
)

# 添加头文件路径
target_include_directories(Lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)