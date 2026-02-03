add_library(platform-ioutils STATIC
    copyfile.c
    getfiles.c
    readfile.c
    writefile.c
)
target_include_directories(platform-ioutils PRIVATE ../include)
