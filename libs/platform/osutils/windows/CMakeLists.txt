add_library(platform-osutils STATIC
    abspath.c
    chsize.c   
    cpucount.c 
    exec.c  
    sleep.c   
    unlink.c 
)
target_include_directories(platform-osutils PRIVATE ../../include)
