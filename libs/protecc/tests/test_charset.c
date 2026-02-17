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

int test_charset_patterns(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    
    // Test 1: Character ranges [a-z]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file[a-z]" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [a-z] pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/filea", 0), 
                   "Should match [a-z] with 'a'");
        TEST_ASSERT(protecc_match(compiled, "/tmp/filez", 0), 
                   "Should match [a-z] with 'z'");
        TEST_ASSERT(protecc_match(compiled, "/tmp/filem", 0), 
                   "Should match [a-z] with 'm'");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/fileA", 0), 
                   "Should not match [a-z] with 'A'");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file1", 0), 
                   "Should not match [a-z] with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 2: Digit ranges [0-9]
    {
        const protecc_pattern_t patterns[] = {{ "/dev/tty[0-9]" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9] pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/dev/tty0", 0), 
                   "Should match [0-9] with '0'");
        TEST_ASSERT(protecc_match(compiled, "/dev/tty5", 0), 
                   "Should match [0-9] with '5'");
        TEST_ASSERT(protecc_match(compiled, "/dev/tty9", 0), 
                   "Should match [0-9] with '9'");
        TEST_ASSERT(!protecc_match(compiled, "/dev/ttya", 0), 
                   "Should not match [0-9] with letter");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 3: Character sets [abc]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/file[abc]" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [abc] pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/filea", 0), 
                   "Should match [abc] with 'a'");
        TEST_ASSERT(protecc_match(compiled, "/tmp/fileb", 0), 
                   "Should match [abc] with 'b'");
        TEST_ASSERT(protecc_match(compiled, "/tmp/filec", 0), 
                   "Should match [abc] with 'c'");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/filed", 0), 
                   "Should not match [abc] with 'd'");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 4: Multiple ranges [a-zA-Z]
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/[a-zA-Z]" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [a-zA-Z] pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/a", 0), 
                   "Should match [a-zA-Z] with 'a'");
        TEST_ASSERT(protecc_match(compiled, "/tmp/Z", 0), 
                   "Should match [a-zA-Z] with 'Z'");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/1", 0), 
                   "Should not match [a-zA-Z] with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 5: Charset with modifiers [0-9]+
    {
        const protecc_pattern_t patterns[] = {{ "/dev/tty[0-9]+" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9]+ pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/dev/tty1", 0), 
                   "Should match [0-9]+ with single digit");
        TEST_ASSERT(protecc_match(compiled, "/dev/tty123", 0), 
                   "Should match [0-9]+ with multiple digits");
        TEST_ASSERT(!protecc_match(compiled, "/dev/tty", 0), 
                   "Should not match [0-9]+ without digits");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 6: Charset with optional modifier [0-9]?
    {
        const protecc_pattern_t patterns[] = {{ "/dev/port[0-9]?" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile [0-9]? pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/dev/port", 0), 
                   "Should match [0-9]? without digit");
        TEST_ASSERT(protecc_match(compiled, "/dev/port5", 0), 
                   "Should match [0-9]? with digit");
        TEST_ASSERT(!protecc_match(compiled, "/dev/port12", 0), 
                   "Should not match [0-9]? with multiple digits");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 7: Complex pattern with charset and wildcards
    {
        const protecc_pattern_t patterns[] = {{ "/var/log/[a-z]*.[0-9]+" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile complex pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/var/log/app.1", 0), 
                   "Should match complex pattern");
        TEST_ASSERT(protecc_match(compiled, "/var/log/system.123", 0), 
                   "Should match complex pattern");
        TEST_ASSERT(!protecc_match(compiled, "/var/log/1app.1", 0), 
                   "Should not match pattern starting with digit");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    // Test 8: Case insensitive flag
    {
        const protecc_pattern_t patterns[] = {{ "/tmp/File" }};
        err = protecc_compile(patterns, 1, PROTECC_FLAG_CASE_INSENSITIVE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile case-insensitive pattern");
        
        TEST_ASSERT(protecc_match(compiled, "/tmp/file", 0), 
                   "Should match case-insensitive");
        TEST_ASSERT(protecc_match(compiled, "/tmp/FILE", 0), 
                   "Should match case-insensitive");
        TEST_ASSERT(protecc_match(compiled, "/tmp/File", 0), 
                   "Should match case-insensitive");
        
        protecc_free(compiled);
        compiled = NULL;
    }
    
    return 0;
}
