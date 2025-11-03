add_library(containerv-windows STATIC
    rootfs/image.c
    container-options.c
    container.c
    hcs.c
    network.c
    utils.c
)
target_include_directories(containerv-windows PRIVATE ../include)
target_link_libraries(containerv-windows PUBLIC platform vlog)
