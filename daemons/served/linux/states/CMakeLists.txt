add_library(served-states STATIC
    prechecks.c
    download.c
)
target_include_directories(served-states PRIVATE ../include)
target_link_libraries(served-states PRIVATE chef-client store platform vlog)
