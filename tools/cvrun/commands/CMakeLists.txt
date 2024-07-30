
add_library(cvrun-commands STATIC
    start.c
    exec.c
)
target_link_libraries(cvrun-commands containerv platform vlog)
