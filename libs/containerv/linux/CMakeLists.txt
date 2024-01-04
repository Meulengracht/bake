add_library(containerv-linux STATIC
    container.c
    kitchen.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC ingredient platform)
