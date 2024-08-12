
add_subdirectory(commands)

set (CVCTL_SRCS
    main.c
)

add_executable(cvctl ${CVCTL_SRCS})
target_link_libraries(cvctl PRIVATE cvctl-commands platform vlog)
