set(SRCS
    bucket_map.c
    entries.c
    format.c
)

add_library(libdisk-mfs STATIC ${SRCS})
