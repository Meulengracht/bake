# Project setup
project (common C)

add_library(common STATIC
    recipes/image.c
    recipes/package.c
    recipes/utils.c

    vafs/image.c
    vafs/ingredient.c
    vafs/metadata.c
    vafs/utils.c

    utils/preprocessor.c
    utils/runtime.c
)
target_include_directories(common PUBLIC include)
target_link_libraries(common PRIVATE platform)
target_link_libraries(common PUBLIC yaml vafs zstd)

# Tests
option(COMMON_BUILD_TESTS "Build common parser tests" ON)
if(COMMON_BUILD_TESTS)
    add_executable(common_parser_test
        tests/test_main.c
        tests/test_manifest.c
        tests/test_recipe_parser.c
        tests/test_image_parser.c
    )
    target_link_libraries(common_parser_test PRIVATE common platform)
    add_test(NAME common_parser_test COMMAND common_parser_test)
endif()
