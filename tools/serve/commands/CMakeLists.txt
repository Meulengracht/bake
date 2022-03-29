
add_library(serve-commands STATIC
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_client.c
    client.c
    install.c
    list.c
    remove.c
    update.c
)
add_dependencies(serve-commands service_client)
target_include_directories(serve-commands PUBLIC ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(serve-commands PUBLIC chef-client gracht_static)
