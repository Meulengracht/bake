add_library(containerv-linux STATIC
    container.c
)
target_include_directories(containerv-linux PRIVATE ../include)
