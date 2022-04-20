add_library(oven-resolvers STATIC
        elf.c
)

target_include_directories(oven-resolvers PRIVATE ../include)
target_link_libraries(oven-resolvers PRIVATE platform)
