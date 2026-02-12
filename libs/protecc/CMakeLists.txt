# Project setup
project(protecc C)

# Create the protecc library
add_library(protecc STATIC
    protecc.c
    node.c
    parser.c
    matcher.c
)

# Include directories
target_include_directories(protecc PUBLIC include)
target_include_directories(protecc PRIVATE .)

# Add tests if building tests is enabled
option(PROTECC_BUILD_TESTS "Build protecc tests" ON)

if(PROTECC_BUILD_TESTS)
    # Test executable
    add_executable(protecc_test
        tests/test_main.c
        tests/test_basic.c
        tests/test_wildcards.c
        tests/test_charset.c
    )
    
    target_link_libraries(protecc_test PRIVATE protecc)
    target_include_directories(protecc_test PRIVATE include .)
    
    # Add test target
    enable_testing()
    add_test(NAME protecc_test COMMAND protecc_test)
endif()
