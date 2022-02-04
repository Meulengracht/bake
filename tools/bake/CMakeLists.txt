set (BAKE_SRCS
    main.c
)

add_executable(bake ${BAKE_SRCS})


# add sub projects
add_subdirectory(commands)
add_subdirectory(parser)

target_include_directories(bake PRIVATE include)
target_link_libraries(bake bake-commands bake-parser liboven libyaml)
