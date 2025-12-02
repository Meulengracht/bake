add_library(containerv-windows STATIC
    rootfs/debootstrap.c
    container-options.c
    container.c
    layers.c
)
target_include_directories(containerv-windows PRIVATE ../include)
target_link_libraries(containerv-windows PUBLIC platform vlog)
target_link_libraries(containerv-windows PUBLIC advapi32 ole32)
