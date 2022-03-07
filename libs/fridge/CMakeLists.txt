# Project setup
project (fridge C)

add_library(fridge STATIC
    fridge.c
    inventory.c
)
target_include_directories(fridge PUBLIC include)
target_link_libraries(fridge PRIVATE platform)
target_link_libraries(fridge PUBLIC chef-client vafs zstd-api libzstd_static)
