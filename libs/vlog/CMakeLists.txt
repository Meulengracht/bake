# Project setup
project (vlog C)

add_library(vlog STATIC
    logf.c
)
target_include_directories(vlog PUBLIC include)
