
add_library(cvctl-commands STATIC
    start.c
    exec.c
)
target_link_libraries(cvctl-commands containerv platform vlog)
