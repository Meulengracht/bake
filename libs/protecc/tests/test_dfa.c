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

static bool match_path(const protecc_profile_t* compiled, const char* path) {
    return protecc_match_path(compiled, path, PROTECC_PERM_NONE);
}

static void setup_dfa_config(protecc_compile_config_t* config) {
    protecc_compile_config_default(config);
    config->mode = PROTECC_COMPILE_MODE_DFA;
    config->max_classes = 256;
}

int test_dfa_patterns(void) {
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    protecc_compile_config_t config;

    setup_dfa_config(&config);

    // Test 1: basic DFA compilation/matching
    {
        const protecc_pattern_t patterns[] = {
            { "/etc/passwd", PROTECC_PERM_ALL },
            { "/tmp/*.txt", PROTECC_PERM_ALL }
        };
        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA basic patterns");

        TEST_ASSERT(match_path(compiled, "/etc/passwd"), "DFA should match literal path");
        TEST_ASSERT(match_path(compiled, "/tmp/file.txt"), "DFA should match wildcard path");
        TEST_ASSERT(!match_path(compiled, "/tmp/file.log"), "DFA should reject non-matching extension");

        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 2: modifier support in DFA mode
    {
        const protecc_pattern_t patterns[] = {
            { "/dev/tty[0-9]+", PROTECC_PERM_ALL },
            { "/dev/port[0-9]?", PROTECC_PERM_ALL },
            { "/var/log/[a-z]*.log", PROTECC_PERM_ALL },
        };

        err = protecc_compile_patterns(patterns, 3, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA modifier patterns");

        TEST_ASSERT(match_path(compiled, "/dev/tty1"), "DFA should match one-or-more (+)");
        TEST_ASSERT(match_path(compiled, "/dev/tty123"), "DFA should match one-or-more (+) repeated");
        TEST_ASSERT(!match_path(compiled, "/dev/tty"), "DFA should reject missing + match");

        TEST_ASSERT(match_path(compiled, "/dev/port"), "DFA should match optional (?) empty");
        TEST_ASSERT(match_path(compiled, "/dev/port7"), "DFA should match optional (?) one char");
        TEST_ASSERT(!match_path(compiled, "/dev/port77"), "DFA should reject optional (?) two chars");

        TEST_ASSERT(match_path(compiled, "/var/log/system.log"), "DFA should match zero-or-more (*)");
        TEST_ASSERT(match_path(compiled, "/var/log/a.log"), "DFA should match zero-or-more single");
        TEST_ASSERT(!match_path(compiled, "/var/log/1.log"), "DFA should reject charset mismatch before *");

        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 3: export/import roundtrip for DFA profiles
    {
        const protecc_pattern_t patterns[] = {
            { "/home/**", PROTECC_PERM_ALL },
            { "/tmp/[a-z]+.txt", PROTECC_PERM_ALL }
        };
        void* blob = NULL;
        size_t blob_size = 0;
        protecc_profile_t* imported = NULL;

        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for export/import");

        err = protecc_profile_export_path(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to query DFA export size");
        TEST_ASSERT(blob_size > 0, "DFA export size should be > 0");

        blob = malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate export blob");

        err = protecc_profile_export_path(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob");

        err = protecc_profile_import_path_blob(blob, blob_size, &imported);
        TEST_ASSERT(err == PROTECC_OK, "Failed to import DFA blob");

        TEST_ASSERT(match_path(imported, "/home/user/docs/file"), "Imported DFA should match recursive path");
        TEST_ASSERT(match_path(imported, "/tmp/abc.txt"), "Imported DFA should match charset+modifier path");
        TEST_ASSERT(!match_path(imported, "/tmp/123.txt"), "Imported DFA should reject non-charset path");

        protecc_free(imported);
        free(blob);
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 4: enforce max_patterns and max_pattern_length
    {
        const protecc_pattern_t patterns_ok[] = { { "/a", PROTECC_PERM_ALL }, { "/b", PROTECC_PERM_ALL } };
        const protecc_pattern_t patterns_too_many[] = {
            { "/a", PROTECC_PERM_ALL },
            { "/b", PROTECC_PERM_ALL },
            { "/c", PROTECC_PERM_ALL }
        };
        const protecc_pattern_t pattern_too_long[] = {
            { "/this/pattern/is/definitely/longer/than/five", PROTECC_PERM_ALL }
        };

        config.max_patterns = 2;
        config.max_pattern_length = 64;

        err = protecc_compile_patterns(patterns_ok, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Compile should pass under max_patterns");
        protecc_free(compiled);
        compiled = NULL;

        err = protecc_compile_patterns(patterns_too_many, 3, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Compile should fail above max_patterns");

        config.max_patterns = 4;
        config.max_pattern_length = 5;
        err = protecc_compile_patterns(pattern_too_long, 1, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Compile should fail above max_pattern_length");
    }

    // Test 5: stress max_states enforcement in DFA mode
    {
        const protecc_pattern_t patterns[] = {
            { "/stress/alpha", PROTECC_PERM_ALL },
            { "/stress/beta", PROTECC_PERM_ALL },
            { "/stress/gamma", PROTECC_PERM_ALL },
            { "/stress/delta", PROTECC_PERM_ALL },
            { "/stress/epsilon", PROTECC_PERM_ALL },
        };

        setup_dfa_config(&config);
        config.max_states = 2;

        err = protecc_compile_patterns(patterns, 5, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_ERROR_COMPILE_FAILED,
                   "DFA compile should fail when max_states cap is exceeded");
    }

    // Test 6: import should reject truncated DFA blob
    {
        const protecc_pattern_t patterns[] = {
            { "/import/test", PROTECC_PERM_ALL },
            { "/tmp/[a-z]+.txt", PROTECC_PERM_ALL }
        };
        void* blob = NULL;
        size_t blob_size = 0;
        protecc_profile_t* imported = NULL;

        setup_dfa_config(&config);
        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for truncated import test");

        err = protecc_profile_export_path(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK && blob_size > 8, "Failed to query DFA export size for truncated import test");

        blob = malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate blob for truncated import test");

        err = protecc_profile_export_path(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob for truncated import test");

        err = protecc_profile_import_path_blob(blob, blob_size - 7, &imported);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject truncated DFA blob");
        TEST_ASSERT(imported == NULL, "Import output should remain NULL on truncated blob");

        free(blob);
        protecc_free(compiled);
        compiled = NULL;
    }

    // Test 7: import should reject malformed DFA metadata
    {
        const protecc_pattern_t patterns[] = {
            { "/import/meta", PROTECC_PERM_ALL },
            { "/dev/tty[0-9]+", PROTECC_PERM_ALL }
        };
        uint8_t* blob = NULL;
        size_t blob_size = 0;
        protecc_profile_header_t* header;
        protecc_profile_dfa_t* dfa;
        protecc_profile_t* imported = NULL;

        setup_dfa_config(&config);
        err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, &config, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA for malformed metadata test");

        err = protecc_profile_export_path(compiled, NULL, 0, &blob_size);
        TEST_ASSERT(err == PROTECC_OK && blob_size >= sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t),
                   "Failed to query DFA export size for malformed metadata test");

        blob = (uint8_t*)malloc(blob_size);
        TEST_ASSERT(blob != NULL, "Failed to allocate blob for malformed metadata test");

        err = protecc_profile_export_path(compiled, blob, blob_size, &blob_size);
        TEST_ASSERT(err == PROTECC_OK, "Failed to export DFA blob for malformed metadata test");

        header = (protecc_profile_header_t*)blob;
        dfa = (protecc_profile_dfa_t*)(blob + sizeof(protecc_profile_header_t));

        // 7a: wrong accept_words should fail
        {
            uint32_t original = dfa->accept_words;
            dfa->accept_words = original + 1u;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject wrong accept_words");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on wrong accept_words");
            dfa->accept_words = original;
        }

        // 7b: out-of-range classmap offset should fail
        {
            uint32_t original = dfa->classmap_off;
            dfa->classmap_off = header->stats.binary_size;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject out-of-range classmap offset");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on bad classmap offset");
            dfa->classmap_off = original;
        }

        // 7c: out-of-range perms offset should fail
        {
            uint32_t original = dfa->perms_off;
            dfa->perms_off = header->stats.binary_size - 1u;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject bad perms offset");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on bad perms offset");
            dfa->perms_off = original;
        }

        // 7d: out-of-range transition offset should fail
        {
            uint32_t original = dfa->transitions_off;
            dfa->transitions_off = header->stats.binary_size - 2u;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject bad transition offset");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on bad transition offset");
            dfa->transitions_off = original;
        }

        // 7e: invalid start_state should fail
        {
            uint32_t original = dfa->start_state;
            dfa->start_state = dfa->num_states;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject invalid start_state");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on invalid start_state");
            dfa->start_state = original;
        }

        // 7f: num_classes == 0 should fail
        {
            uint32_t original = dfa->num_classes;
            dfa->num_classes = 0;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Import should reject num_classes == 0");
            TEST_ASSERT(imported == NULL, "Import output should remain NULL on num_classes == 0");
            dfa->num_classes = original;
        }

        // 7g: num_classes > 256 should fail
        {
            uint32_t original = dfa->num_classes;
            dfa->num_classes = 257u;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
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
        err = protecc_compile_patterns(PROTECC_BRANCHING_PATTERNS,
                              PROTECC_BRANCHING_PATTERNS_COUNT,
                              PROTECC_FLAG_NONE,
                              &config,
                              &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile DFA deep branching parity patterns");

        for (size_t i = 0; i < PROTECC_BRANCHING_CASES_COUNT; i++) {
            bool matched = match_path(compiled, PROTECC_BRANCHING_CASES[i].path);
            TEST_ASSERT((int)matched == PROTECC_BRANCHING_CASES[i].expected_match,
                       "Branching parity case mismatch in DFA test");
        }

        protecc_free(compiled);
        compiled = NULL;
    }

    return 0;
}
