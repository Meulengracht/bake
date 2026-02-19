/**
 * @file test_basic.c
 * @brief Basic pattern tests for protecc library
 */

#include <protecc/protecc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

int test_basic_patterns(void) {
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Exact match
    {
        const protecc_pattern_t patterns[] = {
            { "/etc/passwd", PROTECC_PERM_ALL }
        };
        
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile exact pattern");
        TEST_ASSERT(compiled != NULL, "Compiled result is NULL");
        
        TEST_ASSERT(match_path(compiled, "/etc/passwd"), 
                   "Should match exact path");
        TEST_ASSERT(!match_path(compiled, "/etc/shadow"), 
                   "Should not match different path");
        TEST_ASSERT(!match_path(compiled, "/etc/passwd/extra"), 
                   "Should not match longer path");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Multiple patterns
    {
        const protecc_pattern_t patterns[] = {
            { "/tmp/file1", PROTECC_PERM_ALL },
            { "/tmp/file2", PROTECC_PERM_ALL },
            { "/var/log", PROTECC_PERM_ALL }
        };
        err = protecc_compile_patterns(patterns, 3, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile multiple patterns");
        
        TEST_ASSERT(match_path(compiled, "/tmp/file1"), 
                   "Should match first pattern");
        TEST_ASSERT(match_path(compiled, "/tmp/file2"), 
                   "Should match second pattern");
        TEST_ASSERT(match_path(compiled, "/var/log"), 
                   "Should match third pattern");
        TEST_ASSERT(!match_path(compiled, "/tmp/file3"), 
                   "Should not match non-existent pattern");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 3: Pattern validation
    {
        TEST_ASSERT(protecc_validate_pattern("/etc/passwd") == PROTECC_OK, 
                   "Should validate correct pattern");
        TEST_ASSERT(protecc_validate_pattern("[abc") != PROTECC_OK, 
                   "Should reject unbalanced brackets");
        TEST_ASSERT(protecc_validate_pattern("abc]") != PROTECC_OK, 
                   "Should reject unbalanced brackets");
    }
    
    // Test 4: Error handling
    {
        err = protecc_compile_patterns(NULL, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL patterns");
        
        const protecc_pattern_t patterns[] = {{ "/test", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 0, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with zero count");
        
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, NULL);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL output");
    }
    
    // Test 5: Empty path and edge cases
    {
        const protecc_pattern_t patterns[] = {{ "/", PROTECC_PERM_ALL }};
        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile root pattern");
        
        TEST_ASSERT(match_path(compiled, "/"), "Should match root");
        TEST_ASSERT(!match_path(compiled, ""), "Should not match empty");
        TEST_ASSERT(!match_path(compiled, NULL), "Should not match NULL");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Statistics
    {
        const protecc_pattern_t patterns[] = {
            { "/test1", PROTECC_PERM_ALL },
            { "/test2", PROTECC_PERM_ALL }
        };
        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile for stats test");
        
        protecc_stats_t stats;
        err = protecc_get_stats(compiled, &stats);
        TEST_ASSERT(err == PROTECC_OK, "Failed to get stats");
        TEST_ASSERT(stats.num_patterns == 2, "Wrong pattern count in stats");
        
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 7: Deep pattern regression (iterative matcher stack)
    {
        char pattern[2048];
        char path[2048];
        size_t written = 0;
        protecc_compile_config_t config;
        const protecc_pattern_t patterns[] = {{pattern, PROTECC_PERM_ALL}};

        pattern[0] = '/';
        path[0] = '/';
        written = 1;

        for (int i = 0; i < 180; i++) {
            int rc1 = snprintf(pattern + written, sizeof(pattern) - written, "a/");
            int rc2 = snprintf(path + written, sizeof(path) - written, "a/");
            TEST_ASSERT(rc1 > 0 && rc2 > 0, "Failed to build deep pattern segments");
            written += 2;
        }

        TEST_ASSERT(written + 2 < sizeof(pattern), "Deep pattern buffer too small");
        pattern[written - 1] = '\0';
        path[written - 1] = '\0';

        protecc_compile_config_default(&config);
        config.max_pattern_length = sizeof(pattern) - 1;

        err = protecc_compile_patterns(patterns, 1, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile deep pattern");
        TEST_ASSERT(match_path(compiled, path), "Deep pattern should match deep path");
        TEST_ASSERT(!match_path(compiled, "/a/a/a/b"), "Deep pattern should reject different deep path");

        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
