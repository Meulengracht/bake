# Linux reference implementation of the served daemon
find_package(FUSE REQUIRED)

# add sub-targets
add_subdirectory(application)
add_subdirectory(installer)
add_subdirectory(utils)

set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_server.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set(SRCS
    ${GENERATED_SRCS}
    api.c
    main.c
    shutdown.c
    startup.c
    state.c
)

add_executable(served ${SRCS})
add_dependencies(serve-commands service_server)
target_include_directories(served PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(served
        served-application served-installer served-utils
        gracht jansson-api jansson platform vlog
        zstd-api libzstd_static
        -lpthread ${FUSE_LIBRARIES}
)

install(
        TARGETS served
        RUNTIME DESTINATION bin
)
