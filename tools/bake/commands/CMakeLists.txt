
add_library(bake-commands STATIC
    build.c
    clean.c
    fetch.c
    init.c
    recipe_specification.c
)

target_include_directories(bake-commands PRIVATE ../include)
target_link_libraries(bake-commands kitchen fridge platform)
