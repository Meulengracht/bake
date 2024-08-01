set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_served_service_client.c
        )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}
    client.c
    main.c
)

add_executable(serve-exec ${SRCS})
add_dependencies(serve-exec service_client)
target_include_directories(serve-exec PRIVATE ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(serve-exec PRIVATE containerv chef-client gracht)
