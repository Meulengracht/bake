
add_library(cvctl-commands STATIC
    start.c
    exec.c
    config.c
)
target_link_libraries(cvctl-commands containerv common dirconf jansson platform vlog)
