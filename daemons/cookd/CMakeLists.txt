include (CheckIncludeFiles)
check_include_files (threads.h HAVE_C11_THREADS)
check_include_files (pthread.h HAVE_PTHREAD)

configure_file(cookd-config.h.in cookd-config.h @ONLY)

set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_cook_service_client.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}
    
    server/api.c
    server/notify.c
    server/server.c

    client.c
    config.c
    main.c
)

add_executable(cookd ${SRCS})
add_dependencies(cookd service_client)
target_include_directories(cookd PRIVATE include ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(cookd PRIVATE libcvd libpackage store remote jansson vlog dirconf platform gracht)

install(
    TARGETS cookd
    RUNTIME DESTINATION libexec/chef
)
