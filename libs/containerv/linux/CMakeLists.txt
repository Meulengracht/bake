add_library(containerv-linux STATIC
    rootfs/debootstrap.c
    container-options.c
    container.c
    control-socket.c
    user.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC platform vlog)
target_link_libraries(containerv-linux PUBLIC cap)