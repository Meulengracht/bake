set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_cookd_service_client.c
        ${CMAKE_BINARY_DIR}/protocols/chef_managerd_service_server.c
        ${CMAKE_BINARY_DIR}/protocols/chef_managerd_cook_service_server.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}
    main.c
    server.c
)

add_executable(managerd ${SRCS})
add_dependencies(managerd service_server)
target_include_directories(managerd PRIVATE ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(managerd PRIVATE platform gracht)
