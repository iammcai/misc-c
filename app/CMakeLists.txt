add_library(App STATIC
    led/led.c
    sht30/sht30.c
    ssd1306/ssd1306.c
    sysmon/sysmon.c
)

target_include_directories(App PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../driver
)

target_link_libraries(App PUBLIC
    Lib
    Mgnt
)