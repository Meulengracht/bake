
add_library(oven-builders STATIC
    make.c
)

target_include_directories(oven-builders PRIVATE ../../include)
