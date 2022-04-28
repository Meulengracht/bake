add_library(served-application STATIC
    mount.c
)
target_include_directories(served-application PRIVATE ../include)
