set (MKCDK_SRCS
    main.c
)

add_executable(mkcdk ${MKCDK_SRCS})
target_link_libraries(mkcdk chef-client libdisk common dirconf platform vlog)
