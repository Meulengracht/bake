add_library(platform-utils STATIC
    basename.c
    strbool.c
    strendswith.c
    strfilter.c
    strpathcombine.c
    strreplace.c
    strsplit.c
)
target_include_directories(platform-utils PRIVATE ../include)
