add_library(bakectl-commands STATIC
    build.c
    recreate.c
)

target_include_directories(bakectl-commands PRIVATE ../include)
target_link_libraries(bakectl-commands oven platform)
