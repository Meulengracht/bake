set(MKUVM_SRCS
    main.c
)

add_executable(mkuvm ${MKUVM_SRCS})
target_link_libraries(mkuvm PRIVATE containerv jansson platform vlog)
