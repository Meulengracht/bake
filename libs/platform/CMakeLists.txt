# Project setup
project (platform C)

add_library(platform STATIC
    basename.c
    chdir.c
    chmod.c
    cpucount.c
    fnmatch.c
    getcwd.c
    getenv.c
    getuserdir.c
    isdir.c
    mkdir.c
    readlink.c
    rmdir.c
    setenv.c
    sleep.c
    spawn.c
    stat.c
    strreplace.c
    strsplit.c
    symlink.c
    unsetenv.c
)
target_include_directories(platform PUBLIC include)
