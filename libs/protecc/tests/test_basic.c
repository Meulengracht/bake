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

int test_basic_patterns(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Exact match
    {
        const protecc_pattern_t patterns[] = {
            { "/etc/passwd" }
        };
        
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile exact pattern");
        TEST_ASSERT(compiled != NULL, "Compiled result is NULL");
        
        TEST_ASSERT(protecc_match(compiled, "/etc/passwd", 0), 
                   "Should match exact path");
        TEST_ASSERT(!protecc_match(compiled, "/etc/shadow", 0), 
                   "Should not match different path");
        TEST_ASSERT(!protecc_match(compiled, "/etc/passwd/extra", 0), 
                   "Should not match longer path");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Multiple patterns
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file1" }, { "/tmp/file2" }, { "/var/log" }};
        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile multiple patterns");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/file1", 0), 
                   "Should match first pattern");
        TEST_ASSERT(protecc_match(compiled, "/tmp/file2", 0), 
                   "Should match second pattern");
        TEST_ASSERT(protecc_match(compiled, "/var/log", 0), 
                   "Should match third pattern");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file3", 0), 
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
        err = protecc_compile(NULL, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL patterns");
        
        const protecc_pattern_t patterns[] = {{ "/test" }};
        err = protecc_compile(patterns, 0, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with zero count");
        
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, NULL);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL output");
    }
    
    // Test 5: Empty path and edge cases
    {
        const protecc_pattern_t patterns[] = {{ "/" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile root pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/", 0), "Should match root");
        TEST_ASSERT(!protecc_match(compiled, "", 0), "Should not match empty");
        TEST_ASSERT(!protecc_match(compiled, NULL, 0), "Should not match NULL");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Statistics
    {
        const protecc_pattern_t patterns[] = {{ "/test1" }, { "/test2" }};
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, NULL, &compiled);
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
        const protecc_pattern_t patterns[] = {{pattern}};

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

        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile deep pattern");
        TEST_ASSERT(protecc_match(compiled, path, 0), "Deep pattern should match deep path");
        TEST_ASSERT(!protecc_match(compiled, "/a/a/a/b", 0), "Deep pattern should reject different deep path");

        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
