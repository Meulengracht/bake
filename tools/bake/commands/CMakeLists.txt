set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_waiterd_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

add_library(bake-commands STATIC
    ${GENERATED_SRCS}
    
    # container backend
    build-helpers/build_client.c
    build-helpers/build_context_create.c
    build-helpers/build_step_clean.c
    build-helpers/build_step_container.c
    build-helpers/build_step_make.c
    build-helpers/build_step_source.c
    build.c
    clean.c

    # packing 
    pack-helpers/resolvers/common.c
    pack-helpers/resolvers/elf.c
    pack-helpers/resolvers/pe.c
    pack-helpers/resolvers/resolver_linux.c
    pack-helpers/commands.c
    pack-helpers/container.c
    pack.c

    # stuff
    fridge.c
    init.c
    recipe_specification.c

    # remote backend
    remote-helpers/remote_client.c
    remote-helpers/remote_wizard.c
    remote_build.c
    remote_download.c
    remote_init.c
    remote_resume.c
    remote.c
)

add_dependencies(bake-commands service_client)
target_include_directories(bake-commands PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(bake-commands fridge remote platform gracht)
