add_library(served-utils STATIC
    fuse_vafs.c
)
target_include_directories(served-utils PRIVATE ../include ${ZSTD_INCLUDE_DIR})
target_link_libraries(served-utils PRIVATE vafs ${FUSE_LIBRARIES} ${ZSTD_LIBRARY} vlog)
