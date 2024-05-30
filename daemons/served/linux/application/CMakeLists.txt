add_library(served-application STATIC
    application.c
    commands.c
    mount.c
    paths.c
)
target_compile_definitions(served-application PRIVATE -DCHEF_INSTALL_DIR=\"${CMAKE_INSTALL_PREFIX}\")
target_include_directories(served-application PRIVATE ../include)
target_link_libraries(served-application PUBLIC containerv platform vlog)
