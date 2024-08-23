# Project setup
project (common C)

add_library(common STATIC
    ingredient.c
    package.c
)
target_include_directories(common PUBLIC include)
target_link_libraries(common PRIVATE platform)
target_link_libraries(common PUBLIC vafs zstd-api libzstd_static)
