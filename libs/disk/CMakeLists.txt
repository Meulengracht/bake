
# add filesystem libraries
add_subdirectory(filesystems/fat)

set(SRCS
    diskbuilder.c
)

add_library(libdisk STATIC ${SRCS})
target_include_directories(libdisk PUBLIC include)
target_link_libraries(libdisk PUBLIC libdisk-fat platform vlog)
