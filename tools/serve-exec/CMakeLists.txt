set (SRCS
    main.c
)

add_executable(serve-exec ${SRCS})
target_link_libraries(serve-exec PRIVATE containerv)
