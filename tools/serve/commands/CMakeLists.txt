set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

add_library(serve-commands STATIC
    ${GENERATED_SRCS}
    client.c
    install.c
    list.c
    remove.c
    update.c
)
add_dependencies(serve-commands service_client)
target_include_directories(serve-commands PUBLIC ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(serve-commands PUBLIC chef-client gracht OpenSSL::SSL)
