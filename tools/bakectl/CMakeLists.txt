set (BAKECTL_SRCS
    main.c
)

add_executable(bakectl ${BAKECTL_SRCS})
add_subdirectory(commands)
target_link_libraries(bakectl PRIVATE kitchen bakectl-commands platform vlog)
