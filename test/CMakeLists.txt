add_library(Test STATIC
    test_type_list.c
    test_type_hash.c
    test_mp.c
    test_aq.c
    test_ev_thd.c
    test_ev_lock.c
    test_msg_q.c
    test_heap.c
    test_skiplist.c
    test_thp.c
    test_ev_timer.c
)

target_include_directories(Test PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Test PUBLIC
    Lib
)