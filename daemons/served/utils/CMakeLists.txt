set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_cvd_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

set (SRCS
    cvd_client.c
    helpers.c
    proofs.c
)

if (WIN32)
    list(APPEND SRCS
        paths_win32.c
    )
else()
    list(APPEND SRCS
        paths_linux.c
    )
endif()

add_library(served-utils STATIC
    ${GENERATED_SRCS}
    ${SRCS}
)
add_dependencies(served-utils service_client)
target_include_directories(served-utils PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(served-utils PRIVATE containerv vafs store zstd gracht dirconf platform vlog OpenSSL::SSL)
