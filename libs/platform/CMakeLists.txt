# Project setup
project (platform C)

add_library(libplatform STATIC
        mkdir.c
)
target_include_directories(libplatform PUBLIC include)
