/**
 * @file test_wildcards.c
 * @brief Wildcard pattern tests for protecc library
 */

#include <protecc/protecc.h>
#include <stdio.h>
#include <string.h>

#include "tests/parity_cases.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

static bool match_path(const protecc_profile_t* compiled, const char* path) {
    protecc_permission_t perms = PROTECC_PERM_NONE;

    return protecc_match_path(compiled, path, 0, &perms);
}

int test_wildcard_patterns(void) {
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Single character wildcard (?)
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file?", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile ? pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/file1"), 
                   "Should match with ? (digit)");
        TEST_ASSERT(match_path(compiled, "/tmp/filea"), 
                   "Should match with ? (letter)");
        TEST_ASSERT(!match_path(compiled, "/tmp/file"), 
                   "Should not match without character");
        TEST_ASSERT(!match_path(compiled, "/tmp/file12"), 
                   "Should not match with extra characters");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Multi-character wildcard (*)
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/*.txt", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile * pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/file.txt"), 
                   "Should match *.txt");
        TEST_ASSERT(match_path(compiled, "/tmp/document.txt"), 
                   "Should match *.txt with longer name");
        TEST_ASSERT(!match_path(compiled, "/tmp/file.log"), 
                   "Should not match different extension");
        TEST_ASSERT(!match_path(compiled, "/tmp/sub/file.txt"), 
                   "Should not match across directories with *");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 3: Recursive wildcard (**)
    {
        const protecc_pattern_t patterns[] = {{ "/home/**", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile ** pattern");
        
        TEST_ASSERT(match_path(compiled, "/home/user/file.txt"), 
                   "Should match with ** (nested)");
        TEST_ASSERT(match_path(compiled, "/home/user/docs/file.txt"), 
                   "Should match with ** (deeply nested)");
        TEST_ASSERT(match_path(compiled, "/home/file"), 
                   "Should match with ** (single level)");
        TEST_ASSERT(!match_path(compiled, "/usr/file"), 
                   "Should not match different root");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 4: Mixed wildcards
    {
        const protecc_pattern_t patterns[] = {{ "/var/log/*.log", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile mixed pattern");
        
        TEST_ASSERT(match_path(compiled, "/var/log/system.log"), 
                   "Should match mixed pattern");
        TEST_ASSERT(match_path(compiled, "/var/log/app.log"), 
                   "Should match mixed pattern");
        TEST_ASSERT(!match_path(compiled, "/var/log/sub/app.log"), 
                   "Should not cross directory with *");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 5: Multiple wildcards in pattern
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/*/?.txt", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile multi-wildcard pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/dir/a.txt"), 
                   "Should match multiple wildcards");
        TEST_ASSERT(match_path(compiled, "/tmp/folder/1.txt"), 
                   "Should match multiple wildcards");
        TEST_ASSERT(!match_path(compiled, "/tmp/dir/ab.txt"), 
                   "Should not match wrong ? count");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Wildcard at start and end
    {
        const protecc_pattern_t patterns[] = {
            { "*.log", PROTECC_PERM_ALL },
            { "/tmp/*", PROTECC_PERM_ALL }
        };
        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile start/end wildcard");
        
        TEST_ASSERT(match_path(compiled, "app.log"), 
                   "Should match *.log");
        TEST_ASSERT(match_path(compiled, "system.log"), 
                   "Should match *.log");
        TEST_ASSERT(match_path(compiled, "/tmp/anything"), 
                   "Should match /tmp/*");
        
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 7: Deep branching stress (wildcards + modifiers)
    {
        err = protecc_compile_patterns(PROTECC_BRANCHING_PATTERNS,
                              PROTECC_BRANCHING_PATTERNS_COUNT,
                              PROTECC_FLAG_NONE,
                              NULL,
                              &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile deep branching stress patterns");

        for (size_t i = 0; i < PROTECC_BRANCHING_CASES_COUNT; i++) {
            bool matched = match_path(compiled, PROTECC_BRANCHING_CASES[i].path);
            TEST_ASSERT((int)matched == PROTECC_BRANCHING_CASES[i].expected_match,
                       "Branching parity case mismatch in wildcard test");
        }

        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
