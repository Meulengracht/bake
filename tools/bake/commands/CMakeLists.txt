
add_library(bake-commands STATIC
    fetch.c
    init.c
    pack.c
    recipe_specification.c
)

target_include_directories(bake-commands PRIVATE ../include)
target_link_libraries(bake-commands oven fridge platform)
