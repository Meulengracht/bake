
add_library(bake-commands STATIC
    fetch.c
    init.c
    pack.c
)

target_include_directories(bake-commands PRIVATE ../include)
