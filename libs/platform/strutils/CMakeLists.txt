add_library(platform-strutils STATIC
    basename.c
    strargv.c
    strbool.c
    strendswith.c
    strfilter.c
    strflatten.c
    strpathcombine.c
    strreplace.c
    strsplit.c
)
target_include_directories(platform-strutils PRIVATE ../include)
