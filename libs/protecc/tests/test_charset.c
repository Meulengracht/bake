/**
 * @file test_charset.c
 * @brief Character set and range tests for protecc library
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

static bool match_path(const protecc_compiled_t* compiled, const char* path) {
    protecc_permission_t perms = PROTECC_PERM_NONE;

    return protecc_match(compiled, path, 0, &perms);
}

int test_charset_patterns(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Character ranges [a-z]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file[a-z]", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [a-z] pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/filea"), 
                   "Should match [a-z] with 'a'");
        TEST_ASSERT(match_path(compiled, "/tmp/filez"), 
                   "Should match [a-z] with 'z'");
        TEST_ASSERT(match_path(compiled, "/tmp/filem"), 
                   "Should match [a-z] with 'm'");
        TEST_ASSERT(!match_path(compiled, "/tmp/fileA"), 
                   "Should not match [a-z] with 'A'");
        TEST_ASSERT(!match_path(compiled, "/tmp/file1"), 
                   "Should not match [a-z] with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Digit ranges [0-9]
    {
        const protecc_pattern_t patterns[] = {{ "/dev/tty[0-9]", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9] pattern");
        
        TEST_ASSERT(match_path(compiled, "/dev/tty0"), 
                   "Should match [0-9] with '0'");
        TEST_ASSERT(match_path(compiled, "/dev/tty5"), 
                   "Should match [0-9] with '5'");
        TEST_ASSERT(match_path(compiled, "/dev/tty9"), 
                   "Should match [0-9] with '9'");
        TEST_ASSERT(!match_path(compiled, "/dev/ttya"), 
                   "Should not match [0-9] with letter");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 3: Character sets [abc]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file[abc]", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [abc] pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/filea"), 
                   "Should match [abc] with 'a'");
        TEST_ASSERT(match_path(compiled, "/tmp/fileb"), 
                   "Should match [abc] with 'b'");
        TEST_ASSERT(match_path(compiled, "/tmp/filec"), 
                   "Should match [abc] with 'c'");
        TEST_ASSERT(!match_path(compiled, "/tmp/filed"), 
                   "Should not match [abc] with 'd'");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 4: Multiple ranges [a-zA-Z]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/[a-zA-Z]", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [a-zA-Z] pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/a"), 
                   "Should match [a-zA-Z] with 'a'");
        TEST_ASSERT(match_path(compiled, "/tmp/Z"), 
                   "Should match [a-zA-Z] with 'Z'");
        TEST_ASSERT(!match_path(compiled, "/tmp/1"), 
                   "Should not match [a-zA-Z] with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 5: Charset with modifiers [0-9]+
    {
        const protecc_pattern_t patterns[] = {{ "/dev/tty[0-9]+", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9]+ pattern");
        
        TEST_ASSERT(match_path(compiled, "/dev/tty1"), 
                   "Should match [0-9]+ with single digit");
        TEST_ASSERT(match_path(compiled, "/dev/tty123"), 
                   "Should match [0-9]+ with multiple digits");
        TEST_ASSERT(!match_path(compiled, "/dev/tty"), 
                   "Should not match [0-9]+ without digits");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Charset with optional modifier [0-9]?
    {
        const protecc_pattern_t patterns[] = {{ "/dev/port[0-9]?", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9]? pattern");
        
        TEST_ASSERT(match_path(compiled, "/dev/port"), 
                   "Should match [0-9]? without digit");
        TEST_ASSERT(match_path(compiled, "/dev/port5"), 
                   "Should match [0-9]? with digit");
        TEST_ASSERT(!match_path(compiled, "/dev/port12"), 
                   "Should not match [0-9]? with multiple digits");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 7: Complex pattern with charset and wildcards
    {
        const protecc_pattern_t patterns[] = {{ "/var/log/[a-z]*.[0-9]+", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile complex pattern");
        
        TEST_ASSERT(match_path(compiled, "/var/log/app.1"), 
                   "Should match complex pattern");
        TEST_ASSERT(match_path(compiled, "/var/log/system.123"), 
                   "Should match complex pattern");
        TEST_ASSERT(!match_path(compiled, "/var/log/1app.1"), 
                   "Should not match pattern starting with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 8: Case insensitive flag
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/File", PROTECC_PERM_ALL }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_CASE_INSENSITIVE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile case-insensitive pattern");
        
        TEST_ASSERT(match_path(compiled, "/tmp/file"), 
                   "Should match case-insensitive");
        TEST_ASSERT(match_path(compiled, "/tmp/FILE"), 
                   "Should match case-insensitive");
        TEST_ASSERT(match_path(compiled, "/tmp/File"), 
                   "Should match case-insensitive");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
