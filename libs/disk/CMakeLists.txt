
# add filesystem libraries
add_subdirectory(filesystems/fat)

set(SRCS
    assets/mbr-gpt.c
    assets/mbr.c

    api_fat.c
    api_mfs.c
    diskbuilder.c
)

add_library(libdisk STATIC ${SRCS})
target_include_directories(libdisk PUBLIC include)
target_link_libraries(libdisk PUBLIC libdisk-fat platform vlog)
