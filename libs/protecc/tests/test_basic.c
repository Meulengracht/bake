/**
 * @file test_basic.c
 * @brief Basic pattern tests for protecc library
 */

#include <protecc/protecc.h>
#include <stdio.h>
#include <stdlib.h>
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

    // Test 7: Permissions and export/import
    {
        const protecc_pattern_t patterns[] = {
            {"/etc/**", PROTECC_PERM_READ},
            {"/usr/bin/**", PROTECC_PERM_EXECUTE},
            {"/tmp/**", PROTECC_PERM_READ | PROTECC_PERM_WRITE},
        };
        err = protecc_compile_with_permissions(patterns, 3, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile patterns with permissions");

        TEST_ASSERT(protecc_match_with_permissions(compiled, "/etc/passwd", 0, PROTECC_PERM_READ),
                   "Should allow read for /etc");
        TEST_ASSERT(!protecc_match_with_permissions(compiled, "/etc/passwd", 0, PROTECC_PERM_WRITE),
                   "Should deny write for /etc");
        TEST_ASSERT(protecc_match_with_permissions(compiled, "/usr/bin/ls", 0, PROTECC_PERM_EXECUTE),
                   "Should allow execute for /usr/bin");
        TEST_ASSERT(!protecc_match_with_permissions(compiled, "/usr/bin/ls", 0, PROTECC_PERM_READ),
                   "Should deny read for /usr/bin");
        TEST_ASSERT(protecc_match(compiled, "/tmp/file", 0),
                   "Legacy match should still work without permission filter");

        size_t binary_size = 0;
        err = protecc_export(compiled, NULL, 0, &binary_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to query export size");
        TEST_ASSERT(binary_size > 0, "Exported size should be non-zero");

        void* binary = malloc(binary_size);
        TEST_ASSERT(binary != NULL, "Failed to allocate export buffer");
        err = protecc_export(compiled, binary, binary_size, &binary_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export profile");

        protecc_compiled_t* imported = NULL;
        err = protecc_import(binary, binary_size, &imported);
        TEST_ASSERT(err == PROTECC_OK, "Failed to import profile");
        TEST_ASSERT(protecc_match_with_permissions(imported, "/tmp/file", 0, PROTECC_PERM_WRITE),
                   "Imported profile should preserve permissions");
        TEST_ASSERT(!protecc_match_with_permissions(imported, "/tmp/file", 0, PROTECC_PERM_EXECUTE),
                   "Imported profile should deny missing permissions");

        protecc_free(imported);
        free(binary);
        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
