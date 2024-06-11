add_library(platform-osutils STATIC
    chsize.c    
    exec.c      
)
target_include_directories(platform-osutils PRIVATE ../../include)
