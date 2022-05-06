add_library(served-application STATIC
    application.c
    commands.c
    mount.c
    paths.c
)
target_include_directories(served-application PRIVATE ../include)
target_link_libraries(served-application PUBLIC platform vlog)
