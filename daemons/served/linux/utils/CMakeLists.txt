add_library(served-utils STATIC
    fuse_vafs.c
    proofs.c
)
target_include_directories(served-utils PRIVATE ../include)
target_link_libraries(served-utils PRIVATE vafs ${FUSE_LIBRARIES} store zstd platform vlog OpenSSL::SSL)
