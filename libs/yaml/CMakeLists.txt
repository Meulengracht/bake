# Project setup
project (yaml C)

add_library(yaml STATIC
        api.c
        parser.c
        reader.c
        scanner.c
)

if(WIN32)
    target_compile_definitions(yaml PUBLIC YAML_DECLARE_STATIC=1)
endif()

target_include_directories(yaml PUBLIC include)
