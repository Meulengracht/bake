/**
 * @file test_basic.c
 * @brief Basic pattern tests for protecc library
 */

#include <protecc/protecc.h>
#include <stdio.h>
#include <string.h>

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
        const char* patterns[] = {"/etc/passwd"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
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
        const char* patterns[] = {"/tmp/file1", "/tmp/file2", "/var/log"};
        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, &compiled);
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
        err = protecc_compile(NULL, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL patterns");
        
        const char* patterns[] = {"/test"};
        err = protecc_compile(patterns, 0, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with zero count");
        
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL);
        TEST_ASSERT(err != PROTECC_OK, "Should fail with NULL output");
    }
    
    // Test 5: Empty path and edge cases
    {
        const char* patterns[] = {"/"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile root pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/", 0), "Should match root");
        TEST_ASSERT(!protecc_match(compiled, "", 0), "Should not match empty");
        TEST_ASSERT(!protecc_match(compiled, NULL, 0), "Should not match NULL");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Statistics
    {
        const char* patterns[] = {"/test1", "/test2"};
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile for stats test");
        
        protecc_stats_t stats;
        err = protecc_get_stats(compiled, &stats);
        TEST_ASSERT(err == PROTECC_OK, "Failed to get stats");
        TEST_ASSERT(stats.num_patterns == 2, "Wrong pattern count in stats");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
