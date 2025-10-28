add_library(served-states STATIC
    prechecks.c
)
target_include_directories(served-states PRIVATE ../include)
target_link_libraries(served-states PRIVATE platform vlog)
