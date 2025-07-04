set(SRCS
    fat_access.c
    fat_cache.c
    fat_filelib.c
    fat_format.c
    fat_misc.c
    fat_string.c
    fat_table.c
    fat_write.c
)

add_library(libdisk-fat STATIC ${SRCS})
