set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_cook_service_client.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}
    
    server/server.c

    api.c
    config.c
    main.c
)

add_executable(cookd ${SRCS})
add_dependencies(cookd service_client)
target_include_directories(cookd PRIVATE ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(cookd PRIVATE kitchen fridge remote jansson-api jansson vlog libdirconf platform gracht)
