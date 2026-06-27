add_library(Net STATIC
    zcap/zcap.c
)

target_include_directories(Net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Net PUBLIC
    Lib
    Mgnt
)