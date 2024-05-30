# Project setup
project (ingredient C)

add_library(ingredient STATIC
    ingredient.c
)
target_include_directories(ingredient PUBLIC include)
target_link_libraries(ingredient PRIVATE platform)
target_link_libraries(ingredient PUBLIC vafs zstd-api libzstd_static)
