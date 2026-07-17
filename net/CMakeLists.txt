add_library(Net STATIC
    zcap/zcap.c
    ftx/ftx.c
    nt/arp.c
    nt/ping.c
)

target_include_directories(Net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Net PUBLIC
    Lib
    Mgnt
    m
)