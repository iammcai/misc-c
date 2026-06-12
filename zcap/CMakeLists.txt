add_library(Zcap STATIC
    zcap.c
)

target_link_libraries(Zcap PUBLIC
    Lib
    Mgnt
)