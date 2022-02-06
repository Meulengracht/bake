# Project setup
project (platform C)

add_library(libplatform STATIC
        getenv.c
        mkdir.c
        rmdir.c
        setenv.c
)
target_include_directories(libplatform PUBLIC include)
