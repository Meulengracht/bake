set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_service_server.c
        ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_cook_service_server.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}

    api/cookd.c
    api/waiterd.c

    server/config.c
    server/init.c
    server/server.c

    main.c
)

add_executable(waiterd ${SRCS})
add_dependencies(waiterd service_server)
target_include_directories(waiterd PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(waiterd PRIVATE jansson-api jansson vlog libdirconf platform gracht)
