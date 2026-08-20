# 设置编译的子目录
set(DRIVER_MODULES
    led
    sht30
    ssd1306
)

foreach(mod ${DRIVER_MODULES})
    add_custom_target(${mod}_ko ALL
        COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/${mod}
        COMMENT "[kBuild] Building ${mod}"
    )
endforeach()