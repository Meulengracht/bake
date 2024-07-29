add_library(containerv-linux STATIC
    rootfs/debootstrap.c
    container.c
    user.c
    utils.c
)
target_include_directories(containerv-linux PRIVATE ../include)
target_link_libraries(containerv-linux PUBLIC ingredient platform vlog)
target_link_libraries(containerv-linux PUBLIC cap)