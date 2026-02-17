/**
 * @file parity_cases.h
 * @brief Shared parity datasets for trie and DFA matcher tests
 */

#ifndef PROTECC_TEST_PARITY_CASES_H
#define PROTECC_TEST_PARITY_CASES_H

typedef struct {
    const char* path;
    int         expected_match;
} protecc_parity_case_t;

static const protecc_pattern_t const PROTECC_BRANCHING_PATTERNS[] = {
    { "/stress/**/system.log", 0 },
    { "/stress/*/tmp?/file[0-9]*.txt", 0 },
    { "/stress/[a-z]*/branch/**/end", 0 },
};

static const protecc_parity_case_t PROTECC_BRANCHING_CASES[] = {
    {"/stress/a/b/c/system.log", 1},
    {"/stress/root/tmp1/file123.txt", 1},
    {"/stress/alpha/branch/x/y/z/end", 1},
    {"/stress/root/tmp12/file123.txt", 0},
    {"/stress/1/branch/x/end", 0},
    {"/other/a/b/system.log", 0},
};

#define PROTECC_BRANCHING_PATTERNS_COUNT (sizeof(PROTECC_BRANCHING_PATTERNS) / sizeof(PROTECC_BRANCHING_PATTERNS[0]))
#define PROTECC_BRANCHING_CASES_COUNT    (sizeof(PROTECC_BRANCHING_CASES) / sizeof(PROTECC_BRANCHING_CASES[0]))

#endif /* PROTECC_TEST_PARITY_CASES_H */
