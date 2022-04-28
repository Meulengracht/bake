# Linux reference implementation of the served daemon

# add sub-targets
add_subdirectory(application)

set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_server.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set(SRCS
    ${GENERATED_SRCS}
    api.c
    main.c
    startup.c
    state.c
)

add_executable(served ${SRCS})
add_dependencies(serve-commands service_server)
target_include_directories(served PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(served served-application gracht_static jansson-api jansson platform -lpthread)