# Project setup
project (remote C)

add_library(remote STATIC
    download.c
    pack.c
    unpack.c
    upload.c
)
target_include_directories(remote PUBLIC include)
target_link_libraries(remote PUBLIC chef-client libdirconf vlog platform gracht)
