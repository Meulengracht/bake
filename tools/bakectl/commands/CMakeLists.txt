add_library(bakectl-commands STATIC
    run.c
)

target_include_directories(bakectl-commands PRIVATE ../include)
target_link_libraries(bakectl-commands oven platform)
