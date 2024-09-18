
add_library(bake-commands STATIC
    build.c
    clean.c
    fetch.c
    init.c
    recipe_specification.c
    remote_build.c
    remote_client.c
    remote_init.c
    remote_resume.c
    remote.c
)

target_include_directories(bake-commands PRIVATE ../include)
target_link_libraries(bake-commands kitchen fridge platform gracht)
