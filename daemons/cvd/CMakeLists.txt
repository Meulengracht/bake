set (GENERATED_SRCS
        ${CMAKE_BINARY_DIR}/protocols/chef_cvd_service_server.c
    )
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    ${GENERATED_SRCS}

    server/api.c
    server/server.c

    config.c
    init.c
    main.c
)

# Add platform-specific rootfs implementations
if (UNIX)
    list(APPEND SRCS server/rootfs/debootstrap.c)
elseif(WIN32)
    list(APPEND SRCS server/rootfs/windows/debootstrap.c)
endif()

add_executable(cvd ${SRCS})
add_dependencies(cvd service_server)
target_include_directories(cvd PRIVATE ${CMAKE_BINARY_DIR}/protocols include)
target_link_libraries(cvd PRIVATE containerv jansson vlog libdirconf platform gracht)

install(
    TARGETS cvd
    RUNTIME DESTINATION libexec/chef
)
