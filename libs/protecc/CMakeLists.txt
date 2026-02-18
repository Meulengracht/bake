# Project setup
project(protecc C)

# Create the protecc library
add_library(protecc STATIC
    dfatrie.c
    matcher.c
    node.c
    parser.c
    protecc.c
)

# Include directories
target_include_directories(protecc PUBLIC include)
target_include_directories(protecc PRIVATE .)

# Add tests if building tests is enabled
option(PROTECC_BUILD_TESTS "Build protecc tests" ON)
option(PROTECC_BUILD_EXAMPLES "Build protecc examples" ON)

if(PROTECC_BUILD_TESTS)
    # Test executable
    add_executable(protecc_test
        tests/test_main.c
        tests/test_basic.c
        tests/test_wildcards.c
        tests/test_charset.c
        tests/test_dfa.c
        tests/test_profile_builder.c
    )
    
    target_link_libraries(protecc_test PRIVATE protecc)
    target_include_directories(protecc_test PRIVATE include .)
    
    # Add test target
    enable_testing()
    add_test(NAME protecc_test COMMAND protecc_test)
endif()

if (PROTECC_BUILD_EXAMPLES)
    # Example executable
    add_executable(protecc_example examples/basic.c)
    target_link_libraries(protecc_example PRIVATE protecc)
    target_include_directories(protecc_example PRIVATE include .)
endif()
