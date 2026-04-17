set(MKWBASE_SRCS
    main.c
)

add_executable(mkwbase ${MKWBASE_SRCS})
target_link_libraries(mkwbase PRIVATE jansson platform vlog)
