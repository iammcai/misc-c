add_library(Mgnt STATIC
    cli/cli_trie.c
    cli/cli.c
    cli/cli_gnurl.c
    cli/cli_pager.c
    web/http.c
    web/web.c
    syslog/syslog_client.c
    syslog/syslog_query.c
    syslog/syslog_proc.c
    syslog/syslog_store.c
)

target_include_directories(Mgnt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Mgnt PUBLIC
    readline
    Lib
    Net
    App
)