set (GENERATED_SRCS
    ${CMAKE_BINARY_DIR}/protocols/chef_cvd_service_client.c
)
set_source_files_properties(${GENERATED_SRCS} PROPERTIES GENERATED TRUE)

add_library(served-utils STATIC
    ${GENERATED_SRCS}
    cvd_client.c
    fuse_vafs.c
    helpers.c
    paths.c
    proofs.c
)
add_dependencies(served-utils service_client)
target_include_directories(served-utils PRIVATE ../include ${CMAKE_BINARY_DIR}/protocols)
target_link_libraries(served-utils PRIVATE vafs ${FUSE_LIBRARIES} store zstd gracht dirconf platform vlog OpenSSL::SSL)
