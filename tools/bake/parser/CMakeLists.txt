
add_library(bake-parser STATIC
    recipe.c
)

target_include_directories(bake-parser PRIVATE ../include)
target_link_libraries(bake-parser PRIVATE oven)
target_link_libraries(bake-parser PUBLIC yaml platform)
