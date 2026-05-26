add_library(Test STATIC
    test_type_list.c
)

target_include_directories(Test PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Test PUBLIC
    Lib
)