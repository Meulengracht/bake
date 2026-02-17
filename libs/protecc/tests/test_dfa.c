/**
 * @file test_dfa.c
 * @brief DFA compilation and matching tests for protecc library
 */

#include <protecc/protecc.h>
#include <protecc/profile.h>
#include <stdio.h>
#include <stdlib.h>

#include "tests/parity_cases.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

static void setup_dfa_config(protecc_compile_config_t* config) {
    protecc_compile_config_default(config);
    config->mode = PROTECC_COMPILE_MODE_DFA;
    config->max_classes = 256;
}

int test_dfa_patterns(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    protecc_compile_config_t config;

    setup_dfa_config(&config);

    // Test 1: basic DFA compilation/matching
    {
        const protecc_pattern_t patterns[] = {
            { "/etc/passwd", 0 },
            { "/tmp/*.txt", 0 }
        };
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA basic patterns");

        TEST_ASSERT(protecc_match(compiled, "/etc/passwd", 0), "DFA should match literal path");
        TEST_ASSERT(protecc_match(compiled, "/tmp/file.txt", 0), "DFA should match wildcard path");
        TEST_ASSERT(!protecc_match(compiled, "/tmp/file.log", 0), "DFA should reject non-matching extension");

        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 2: modifier support in DFA mode
    {
        const protecc_pattern_t patterns[] = {
            { "/dev/tty[0-9]+", 0 },
            { "/dev/port[0-9]?", 0 },
            { "/var/log/[a-z]*.log", 0 },
        };

        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA modifier patterns");

        TEST_ASSERT(protecc_match(compiled, "/dev/tty1", 0), "DFA should match one-or-more (+)");
        TEST_ASSERT(protecc_match(compiled, "/dev/tty123", 0), "DFA should match one-or-more (+) repeated");
        TEST_ASSERT(!protecc_match(compiled, "/dev/tty", 0), "DFA should reject missing + match");

        TEST_ASSERT(protecc_match(compiled, "/dev/port", 0), "DFA should match optional (?) empty");
        TEST_ASSERT(protecc_match(compiled, "/dev/port7", 0), "DFA should match optional (?) one char");
        TEST_ASSERT(!protecc_match(compiled, "/dev/port77", 0), "DFA should reject optional (?) two chars");

        TEST_ASSERT(protecc_match(compiled, "/var/log/system.log", 0), "DFA should match zero-or-more (*)");
        TEST_ASSERT(protecc_match(compiled, "/var/log/a.log", 0), "DFA should match zero-or-more single");
        TEST_ASSERT(!protecc_match(compiled, "/var/log/1.log", 0), "DFA should reject charset mismatch before *");

        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 3: export/import roundtrip for DFA profiles
    {
        const protecc_pattern_t patterns[] = {
            { "/home/**", 0 },
            { "/tmp/[a-z]+.txt", 0 }
        };
        void* blob = NULL;
        size_t blob_size = 0;
        protecc_compiled_t* imported = NULL;

        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for export/import");

        err = protecc_export(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to query DFA export size");
        TEST_ASSERT(blob_size > 0, "DFA export size should be > 0");

        blob = malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate export blob");

        err = protecc_export(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob");

        err = protecc_import(blob, blob_size, &imported);
        TEST_ASSERT(err == PROTECC_OK, "Failed to import DFA blob");

        TEST_ASSERT(protecc_match(imported, "/home/user/docs/file", 0), "Imported DFA should match recursive path");
        TEST_ASSERT(protecc_match(imported, "/tmp/abc.txt", 0), "Imported DFA should match charset+modifier path");
        TEST_ASSERT(!protecc_match(imported, "/tmp/123.txt", 0), "Imported DFA should reject non-charset path");

        protecc_free(imported);
        free(blob);
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 4: enforce max_patterns and max_pattern_length
    {
        const protecc_pattern_t patterns_ok[] = { { "/a", 0 }, { "/b", 0 } };
        const protecc_pattern_t patterns_too_many[] = { { "/a", 0 }, { "/b", 0 }, { "/c", 0 } };
        const protecc_pattern_t pattern_too_long[] = { { "/this/pattern/is/definitely/longer/than/five", 0 } };

        config.max_patterns = 2;
        config.max_pattern_length = 64;

        err = protecc_compile(patterns_ok, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Compile should pass under max_patterns");
        protecc_free(compiled);
        compiled = NULL;

        err = protecc_compile(patterns_too_many, 3, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Compile should fail above max_patterns");

        config.max_patterns = 4;
        config.max_pattern_length = 5;
        err = protecc_compile(pattern_too_long, 1, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Compile should fail above max_pattern_length");
    }

    // Test 5: stress max_states enforcement in DFA mode
    {
        const protecc_pattern_t patterns[] = {
            { "/stress/alpha", 0 },
            { "/stress/beta", 0 },
            { "/stress/gamma", 0 },
            { "/stress/delta", 0 },
            { "/stress/epsilon", 0 },
        };

        setup_dfa_config(&config);
        config.max_states = 2;

        err = protecc_compile(patterns, 5, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_COMPILE_FAILED,
                   "DFA compile should fail when max_states cap is exceeded");
    }

    // Test 6: import should reject truncated DFA blob
    {
        const protecc_pattern_t patterns[] = { { "/import/test", 0 }, { "/tmp/[a-z]+.txt", 0 } };
        void* blob = NULL;
        size_t blob_size = 0;
        protecc_compiled_t* imported = NULL;

        setup_dfa_config(&config);
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for truncated import test");

        err = protecc_export(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK && blob_size > 8, "Failed to query DFA export size for truncated import test");

        blob = malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate blob for truncated import test");

        err = protecc_export(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob for truncated import test");

        err = protecc_import(blob, blob_size - 7, &imported);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject truncated DFA blob");
        TEST_ASSERT(imported == NULL, "Import output should remain NULL on truncated blob");

        free(blob);
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 7: import should reject malformed DFA metadata
    {
        const protecc_pattern_t patterns[] = { { "/import/meta", 0 }, { "/dev/tty[0-9]+", 0 } };
        uint8_t* blob = NULL;
        size_t blob_size = 0;
        protecc_profile_header_t* header;
        protecc_profile_dfa_t* dfa;
        protecc_compiled_t* imported = NULL;

        setup_dfa_config(&config);
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for malformed metadata test");

        err = protecc_export(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK && blob_size >= sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t),
                   "Failed to query DFA export size for malformed metadata test");

        blob = (uint8_t*)malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate blob for malformed metadata test");

        err = protecc_export(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob for malformed metadata test");

        header = (protecc_profile_header_t*)blob;
        dfa = (protecc_profile_dfa_t*)(blob + sizeof(protecc_profile_header_t));

        // 7a: wrong accept_words should fail
        {
            uint32_t original = dfa->accept_words;
            dfa->accept_words = original + 1u;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject wrong accept_words");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on wrong accept_words");
            dfa->accept_words = original;
        }

        // 7b: out-of-range classmap offset should fail
        {
            uint32_t original = dfa->classmap_off;
            dfa->classmap_off = header->stats.binary_size;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject out-of-range classmap offset");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on bad classmap offset");
            dfa->classmap_off = original;
        }

        // 7c: out-of-range transition offset should fail
        {
            uint32_t original = dfa->transitions_off;
            dfa->transitions_off = header->stats.binary_size - 2u;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject bad transition offset");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on bad transition offset");
            dfa->transitions_off = original;
        }

        // 7d: invalid start_state should fail
        {
            uint32_t original = dfa->start_state;
            dfa->start_state = dfa->num_states;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject invalid start_state");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on invalid start_state");
            dfa->start_state = original;
        }

        // 7e: num_classes == 0 should fail
        {
            uint32_t original = dfa->num_classes;
            dfa->num_classes = 0;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject num_classes == 0");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on num_classes == 0");
            dfa->num_classes = original;
        }

        // 7f: num_classes > 256 should fail
        {
            uint32_t original = dfa->num_classes;
            dfa->num_classes = 257u;
            err = protecc_import(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject num_classes > 256");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on num_classes > 256");
            dfa->num_classes = original;
        }

        free(blob);
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 8: DFA parity for deep branching wildcard/modifier patterns
    {
        setup_dfa_config(&config);
        err = protecc_compile(PROTECC_BRANCHING_PATTERNS,
                              PROTECC_BRANCHING_PATTERNS_COUNT,
                              PROTECC_FLAG_NONE,
                              &config,
                              &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA deep branching parity patterns");

        for (size_t i = 0; i < PROTECC_BRANCHING_CASES_COUNT; i++) {
            bool matched = protecc_match(compiled, PROTECC_BRANCHING_CASES[i].path, 0);
            TEST_ASSERT((int)matched == PROTECC_BRANCHING_CASES[i].expected_match,
                       "Branching parity case mismatch in DFA test");
        }

        protecc_free(compiled);
        compiled = NULL;
    }

    return 0;
}
