# Project setup
project (platform C)

add_library(platform STATIC
    basename.c
    chdir.c
    chmod.c
    cpucount.c
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
    strfilter.c
    strpathcombine.c
    strreplace.c
    strsplit.c
    symlink.c
    unsetenv.c
)
target_include_directories(platform PUBLIC include)
