
set (MKCDK_SRCS
    main.c
)

add_executable(mkcdk ${MKCDK_SRCS})
target_link_libraries(mkcdk PRIVATE platform vlog)
