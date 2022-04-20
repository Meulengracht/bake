# Project setup
project (platform C)

add_subdirectory(strutils)

add_library(platform STATIC
    chdir.c
    chmod.c
    cpucount.c
    getcwd.c
    getuserdir.c
    isdir.c
    mkdir.c
    readlink.c
    rmdir.c
    script.c
    sleep.c
    spawn.c
    stat.c
    symlink.c
)
target_include_directories(platform PUBLIC include)
target_link_libraries(platform PUBLIC platform-utils)
