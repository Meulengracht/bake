add_library(served-states STATIC
    cancelled.c
    completed.c
    dependencies.c
    download.c
    error.c
    generate-wrappers.c
    install.c
    load.c
    mount.c
    prechecks.c
    remove-wrappers.c
    start-services.c
    stop-services.c
    uninstall.c
    unload.c
    unmount.c
    update.c
    verify.c
)
target_include_directories(served-states PRIVATE ../include)
target_link_libraries(served-states PRIVATE store platform vlog)
