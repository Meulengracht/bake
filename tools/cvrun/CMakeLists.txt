set (CVRUN_SRCS
    main.c
)

add_executable(cvrun ${CVRUN_SRCS})
target_link_libraries(cvrun PRIVATE containerv)
