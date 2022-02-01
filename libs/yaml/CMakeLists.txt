# Project setup
project (yaml C)

add_library(libyaml STATIC
        api.c
        parser.c
        reader.c
        scanner.c
)
target_include_directories(libyaml PUBLIC include)
