
add_library(bake-commands STATIC
    recipes/app.c
    recipes/library.c

    fetch.c
    init.c
    pack.c
)

target_include_directories(bake-commands PRIVATE ../include)
target_link_libraries(bake-commands liboven)
