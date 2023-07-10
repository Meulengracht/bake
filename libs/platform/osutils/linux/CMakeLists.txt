add_library(platform-osutils STATIC
    abspath.c
    chmod.c
    chsize.c
    cpucount.c
    exec.c
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
    unlink.c
)
target_include_directories(platform-osutils PRIVATE ../../include)
