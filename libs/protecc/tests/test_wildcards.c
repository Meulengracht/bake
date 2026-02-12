/**
 * @file test_wildcards.c
 * @brief Wildcard pattern tests for protecc library
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

int test_wildcard_patterns(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Single character wildcard (?)
    {
        const char* patterns[] = {"/tmp/file?"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile ? pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/file1", 0), 
                   "Should match with ? (digit)");
        TEST_ASSERT(protecc_match(compiled, "/tmp/filea", 0), 
                   "Should match with ? (letter)");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file", 0), 
                   "Should not match without character");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file12", 0), 
                   "Should not match with extra characters");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Multi-character wildcard (*)
    {
        const char* patterns[] = {"/tmp/*.txt"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile * pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/file.txt", 0), 
                   "Should match *.txt");
        TEST_ASSERT(protecc_match(compiled, "/tmp/document.txt", 0), 
                   "Should match *.txt with longer name");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file.log", 0), 
                   "Should not match different extension");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/sub/file.txt", 0), 
                   "Should not match across directories with *");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 3: Recursive wildcard (**)
    {
        const char* patterns[] = {"/home/**"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile ** pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/home/user/file.txt", 0), 
                   "Should match with ** (nested)");
        TEST_ASSERT(protecc_match(compiled, "/home/user/docs/file.txt", 0), 
                   "Should match with ** (deeply nested)");
        TEST_ASSERT(protecc_match(compiled, "/home/file", 0), 
                   "Should match with ** (single level)");
        TEST_ASSERT(!protecc_match(compiled, "/usr/file", 0), 
                   "Should not match different root");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 4: Mixed wildcards
    {
        const char* patterns[] = {"/var/log/*.log"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile mixed pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/var/log/system.log", 0), 
                   "Should match mixed pattern");
        TEST_ASSERT(protecc_match(compiled, "/var/log/app.log", 0), 
                   "Should match mixed pattern");
        TEST_ASSERT(!protecc_match(compiled, "/var/log/sub/app.log", 0), 
                   "Should not cross directory with *");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 5: Multiple wildcards in pattern
    {
        const char* patterns[] = {"/tmp/*/?.txt"};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile multi-wildcard pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/dir/a.txt", 0), 
                   "Should match multiple wildcards");
        TEST_ASSERT(protecc_match(compiled, "/tmp/folder/1.txt", 0), 
                   "Should match multiple wildcards");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/dir/ab.txt", 0), 
                   "Should not match wrong ? count");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Wildcard at start and end
    {
        const char* patterns[] = {"*.log", "/tmp/*"};
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile start/end wildcard");
        
        TEST_ASSERT(protecc_match(compiled, "app.log", 0), 
                   "Should match *.log");
        TEST_ASSERT(protecc_match(compiled, "system.log", 0), 
                   "Should match *.log");
        TEST_ASSERT(protecc_match(compiled, "/tmp/anything", 0), 
                   "Should match /tmp/*");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
