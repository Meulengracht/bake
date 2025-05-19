add_library(bakectl-commands STATIC
    build.c
    clean.c
    common.c
    init.c
    source.c
)

target_include_directories(bakectl-commands PRIVATE ../include)
target_link_libraries(bakectl-commands oven jansson platform)
