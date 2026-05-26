# 添加源文件
add_library(Lib STATIC
    type/type_list.c
)

# 添加头文件路径
target_include_directories(Lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)