add_library(Mgnt STATIC
    cli/cli_trie.c
    cli/cli.c
    cli/cli_gnurl.c
    web/web.c
)

target_include_directories(Mgnt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Mgnt PUBLIC
    readline
    Lib
)