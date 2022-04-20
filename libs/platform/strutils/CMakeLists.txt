add_library(platform-utils STATIC
    basename.c
    strfilter.c
    strpathcombine.c
    strreplace.c
    strsplit.c
)
target_include_directories(platform-utils PRIVATE ../include)
