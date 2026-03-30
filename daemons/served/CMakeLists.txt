# add sub-targets
add_subdirectory(api)
add_subdirectory(state)
add_subdirectory(states)
add_subdirectory(utils)

set(SRCS
    main.c
    shutdown.c
    startup.c
)

add_executable(served ${SRCS})
add_dependencies(served service_server)
target_include_directories(served PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(served
        served-api served-state served-states served-utils
        chef-client store gracht dirconf platform vlog zstd
)

install(
        TARGETS served
        RUNTIME DESTINATION libexec/chef
)
