add_library(platform-osutils STATIC
    chsize.c    
    exec.c  
    sleep.c    
)
target_include_directories(platform-osutils PRIVATE ../../include)
