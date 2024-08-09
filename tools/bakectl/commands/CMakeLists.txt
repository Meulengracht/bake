add_library(bakectl-commands STATIC
    build.c
    clean.c
)

target_include_directories(bakectl-commands PRIVATE ../include)
target_link_libraries(bakectl-commands kitchen oven platform)
