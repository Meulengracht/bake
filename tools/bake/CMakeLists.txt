set (BAKE_SRCS
    main.c
)

add_executable(bake ${BAKE_SRCS})
add_subdirectory(commands)
target_link_libraries(bake PRIVATE bake-commands libdirconf platform vlog)
