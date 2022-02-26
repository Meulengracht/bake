# Project setup
project (platform C)

add_library(platform STATIC
    chdir.c
    getcwd.c
    getenv.c
    isdir.c
    mkdir.c
    rmdir.c
    setenv.c
    sleep.c
    spawn.c
    unsetenv.c
)
target_include_directories(platform PUBLIC include)
