# Project setup
project (yaml C)

add_library(yaml STATIC
        api.c
        parser.c
        reader.c
        scanner.c
)
target_include_directories(yaml PUBLIC include)
