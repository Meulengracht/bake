# Project setup
project(protecc C)

# Create the protecc library
add_library(protecc STATIC
    matchers/dfa.c
    matchers/glob.c
    matchers/trie.c
    profiles/paths/dfa.c
    profiles/paths/dfatrie.c
    profiles/paths/node.c
    profiles/paths/parser.c
    profiles/paths/trie.c
    profiles/builder.c
    profiles/mount.c
    profiles/net.c
    profiles/path.c
    profiles/utils.c
    protecc.c
)

# Include directories
target_include_directories(protecc PUBLIC include)
target_include_directories(protecc PRIVATE .)
target_link_libraries(protecc PUBLIC platform)

# Add tests if building tests is enabled
option(PROTECC_BUILD_TESTS "Build protecc tests" ON)

if(PROTECC_BUILD_TESTS)
    # Test executable
    add_executable(protecc_test
        tests/test_main.c
        tests/test_basic.c
        tests/test_wildcards.c
        tests/test_charset.c
        tests/test_dfa.c
        tests/test_trie.c
        tests/test_profile_builder.c
        tests/test_profile_builder_paths.c
        tests/test_profile_builder_serialization.c
        tests/test_profile_builder_negatives.c
        tests/test_profile_builder_net.c
        tests/test_profile_builder_mount.c
    )
    
    target_link_libraries(protecc_test PRIVATE protecc)
    target_include_directories(protecc_test PRIVATE include .)
    
    # Add test target
    enable_testing()
    add_test(NAME protecc_test COMMAND protecc_test)
endif()
