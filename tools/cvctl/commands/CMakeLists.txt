
add_library(cvctl-commands STATIC
    start.c
    exec.c
    config.c
    uvm.c
)
target_link_libraries(cvctl-commands containerv dirconf jansson platform vlog)
