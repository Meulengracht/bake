add_library(platform-ioutils STATIC
    copydir.c
    copyfile.c
    getfiles.c
    readfile.c
    writefile.c
)
target_include_directories(platform-ioutils PRIVATE ../include)
