# Linux reference implementation of the served daemon

add_library(containerv-linux STATIC
    cgroups.c
    container-options.c
    container.c
    control-socket.c
    layers.c
    monitoring.c
    network.c
    seccomp.c
    user.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC platform vlog)
target_link_libraries(containerv-linux PUBLIC cap seccomp)
target_link_libraries(containerv-linux PUBLIC containerv-common containerv-pid1 containerv-ebpf common)
