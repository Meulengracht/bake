add_library(platform-osutils STATIC
    abspath.c
    chdir.c
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
    sleep.c
    spawn.c
    stat.c
    strdup.c
    strndup.c
    symlink.c
    unlink.c
)
target_include_directories(platform-osutils PRIVATE ../../include)
