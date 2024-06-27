add_library(platform-osutils STATIC
    abspath.c
    chsize.c
    cpucount.c
    exec.c
    getcwd.c
    getuserdir.c
    mkdir.c
    sleep.c
    strdup.c
    strndup.c
    unlink.c
)
target_include_directories(platform-osutils PRIVATE ../../include)
