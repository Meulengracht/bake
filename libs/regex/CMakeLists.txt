# Project setup
project (regex C)

add_library(regex STATIC
    regcomp.c
    regerror.c
    regexec.c
    regfree.c
)
target_include_directories(regex PUBLIC include)
