/**
 * @file test_trie.c
 * @brief Trie import/export tests for protecc library
 */

#include <protecc/protecc.h>
#include <protecc/profile.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

int test_trie_import_patterns(void)
{
    protecc_profile_t* compiled = NULL;
    protecc_profile_t* imported = NULL;
    protecc_error_t err;
    const protecc_pattern_t patterns[] = {
        {"/srv/**", PROTECC_PERM_READ},
        {"/srv/bin/*", PROTECC_PERM_EXECUTE}
    };
    uint8_t* blob = NULL;
    size_t blob_size = 0;

    err = protecc_compile_patterns(patterns, 2, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL, "Failed to compile trie profile");

    err = protecc_profile_export_path(compiled, NULL, 0, &blob_size);
    TEST_ASSERT(err == PROTECC_OK && blob_size > sizeof(protecc_profile_header_t),
                "Failed to query trie export size");

    blob = (uint8_t*)malloc(blob_size);
    TEST_ASSERT(blob != NULL, "Failed to allocate trie export blob");

    err = protecc_profile_export_path(compiled, blob, blob_size, &blob_size);
    TEST_ASSERT(err == PROTECC_OK, "Failed to export trie profile blob");

    err = protecc_profile_import_path_blob(blob, blob_size, &imported);
    TEST_ASSERT(err == PROTECC_OK && imported != NULL, "Failed to import trie profile blob");

    TEST_ASSERT(protecc_match_path(imported, "/srv/docs/readme", PROTECC_PERM_READ),
                "Imported trie should match READ path");
    TEST_ASSERT(protecc_match_path(imported, "/srv/bin/tool", PROTECC_PERM_EXECUTE),
                "Imported trie should match EXEC path");
    TEST_ASSERT(!protecc_match_path(imported, "/srv/bin/tool", PROTECC_PERM_WRITE),
                "Imported trie should reject unmatched permission");

    {
        protecc_profile_header_t* header = (protecc_profile_header_t*)blob;
        protecc_profile_node_t* nodes =
            (protecc_profile_node_t*)(blob + sizeof(protecc_profile_header_t));
        uint32_t* edges =
            (uint32_t*)(blob + sizeof(protecc_profile_header_t)
                        + (size_t)header->num_nodes * sizeof(protecc_profile_node_t));

        err = protecc_profile_import_path_blob(blob, blob_size - 1u, &imported);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Trie import should reject truncated blob");

        {
            uint32_t saved_magic = header->magic;
            header->magic ^= 0xFFu;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Trie import should reject invalid magic");
            header->magic = saved_magic;
        }

        {
            uint32_t saved_flags = header->flags;
            header->flags &= ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Trie import should reject missing profile type flag");
            header->flags = saved_flags;
        }

        {
            uint32_t saved_root = header->root_index;
            header->root_index = header->num_nodes;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Trie import should reject out-of-range root index");
            header->root_index = saved_root;
        }

        if (header->num_nodes > 0) {
            uint32_t saved_start = nodes[0].child_start;
            uint16_t saved_count = nodes[0].child_count;

            nodes[0].child_start = header->num_edges;
            nodes[0].child_count = 1u;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Trie import should reject out-of-range child_start/child_count");

            nodes[0].child_start = saved_start;
            nodes[0].child_count = saved_count;
        }

        if (header->num_edges > 0) {
            uint32_t saved_edge = edges[0];
            edges[0] = header->num_nodes;
            err = protecc_profile_import_path_blob(blob, blob_size, &imported);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Trie import should reject edge target outside node table");
            edges[0] = saved_edge;
        }
    }

    TEST_ASSERT(protecc_profile_import_path_blob(blob, blob_size, NULL) == PROTECC_ERROR_INVALID_ARGUMENT,
                "Trie import should reject NULL output pointer");

    TEST_ASSERT(protecc_error_string(PROTECC_OK) != NULL,
                "Expected error string for PROTECC_OK");
    TEST_ASSERT(protecc_error_string(PROTECC_ERROR_INVALID_ARGUMENT) != NULL,
                "Expected error string for invalid argument");
    TEST_ASSERT(protecc_error_string(PROTECC_ERROR_INVALID_BLOB) != NULL,
                "Expected error string for invalid blob");

    protecc_free(imported);
    protecc_free(compiled);
    free(blob);
    return 0;
}