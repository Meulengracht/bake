/**
 * @file test_main.c
 * @brief Main test runner for protecc library
 */

#include <stdio.h>
#include <stdlib.h>

// Test function declarations
extern int test_basic_patterns(void);
extern int test_wildcard_patterns(void);
extern int test_charset_patterns(void);
extern int test_dfa_patterns(void);

typedef struct {
    const char* name;
    int (*func)(void);
} test_case_t;

static const test_case_t tests[] = {
    {"Basic patterns", test_basic_patterns},
    {"Wildcard patterns", test_wildcard_patterns},
    {"Charset patterns", test_charset_patterns},
    {"DFA patterns", test_dfa_patterns},
};

static const size_t num_tests = sizeof(tests) / sizeof(tests[0]);

int main(int argc, char** argv) {
    int passed = 0;
    int failed = 0;
    
    printf("Running protecc tests...\n\n");
    
    for (size_t i = 0; i < num_tests; i++) {
        printf("Running: %s\n", tests[i].name);
        int result = tests[i].func();
        if (result == 0) {
            printf("  ✓ PASSED\n\n");
            passed++;
        } else {
            printf("  ✗ FAILED\n\n");
            failed++;
        }
    }
    
    printf("=================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("=================================\n");
    
    return failed > 0 ? 1 : 0;
}
