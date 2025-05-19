
add_subdirectory(kitchen)
add_subdirectory(commands)

set (BAKECTL_SRCS
    main.c
)

add_executable(bakectl ${BAKECTL_SRCS})
target_link_libraries(bakectl PRIVATE bakectl-commands libbake platform vlog)
