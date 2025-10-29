set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

add_library(bake-commands STATIC
    ${GENERATED_SRCS}
    
    # container backend
    build.c
    clean.c
    pack.c

    # stuff
    store.c
    init.c
    recipe_specification.c

    # remote backend
    remote-helpers/remote_client.c
    remote-helpers/remote_wizard.c
    remote_build.c
    remote_download.c
    remote_init.c
    remote_resume.c
    remote_list.c
    remote_info.c
    remote.c
)

add_dependencies(bake-commands service_client)
target_include_directories(bake-commands PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(bake-commands libcvd libpackage store remote platform gracht)
