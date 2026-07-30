add_library(App STATIC
    led/led.c
)

target_include_directories(App PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../driver
)

target_link_libraries(App PUBLIC
    Lib
    Mgnt
)