# Project setup
project (platform C)

add_library(libplatform STATIC
    chdir.c
    getcwd.c
    getenv.c
    mkdir.c
    rmdir.c
    setenv.c
    spawn.c
)
target_include_directories(libplatform PUBLIC include)
