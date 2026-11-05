add_library(Net STATIC
    net_comm.c
    zcap/zcap.c
    zcap/zcap_stats.c
    ftx/ftx.c
    nt/arp.c
    nt/ping.c
    nt/tftp.c
    cmcp/cmcp_pkt.c
    cmcp/cmcp_fsm.c
    cmcp/cmcp.c
    dissect/tcp.c
)

target_include_directories(Net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Net PUBLIC
    Lib
    Mgnt
    m
)