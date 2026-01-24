# Linux reference implementation of the served daemon
find_package(FUSE REQUIRED)

add_library(containerv-linux STATIC
    cgroups.c
    container-options.c
    container.c
    control-socket.c
    layers.c
    network.c
    policy-ebpf.c
    seccomp.c
    user.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC platform vlog vafs ${FUSE_LIBRARIES})
target_link_libraries(containerv-linux PUBLIC cap seccomp)
