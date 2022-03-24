# include sub projects for serve
add_subdirectory(commands)

set (SERVE_SRCS
    main.c
)

add_executable(serve ${SERVE_SRCS})
target_link_libraries(serve serve-commands)
