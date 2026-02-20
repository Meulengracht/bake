/**
 * @file test_profile_builder_negatives.c
 * @brief Negative-path and iterator coverage profile builder tests
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

int test_profile_builder_domain_only_and_negatives(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    protecc_net_rule_t net_rules[] = {
        {
            .action = PROTECC_ACTION_ALLOW,
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip_pattern = "10.1.*.*",
            .port_from = 1000,
            .port_to = 1000,
            .unix_path_pattern = NULL
        },
        {
            .action = PROTECC_ACTION_DENY,
            .protocol = PROTECC_NET_PROTOCOL_UDP,
            .family = PROTECC_NET_FAMILY_IPV6,
            .ip_pattern = "fd00::*",
            .port_from = 5353,
            .port_to = 5353,
            .unix_path_pattern = NULL
        }
    };
    protecc_mount_rule_t mount_rules[] = {
        {
            .action = PROTECC_ACTION_ALLOW,
            .source_pattern = "/dev/sda*",
            .target_pattern = "/mnt/a*",
            .fstype_pattern = "ext4",
            .options_pattern = "rw",
            .flags = 1
        },
        {
            .action = PROTECC_ACTION_AUDIT,
            .source_pattern = "/dev/sdb*",
            .target_pattern = "/mnt/b*",
            .fstype_pattern = "xfs",
            .options_pattern = "ro",
            .flags = 2
        }
    };
    uint8_t* net_buffer = NULL;
    uint8_t* mount_buffer = NULL;
    size_t net_export_size = 0;
    size_t mount_export_size = 0;

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder");

    err = protecc_profile_builder_add_net_rule(builder, &net_rules[0]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add first net rule for iterator gap test");
    err = protecc_profile_builder_add_net_rule(builder, &net_rules[1]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add second net rule for iterator gap test");

    err = protecc_profile_builder_add_mount_rule(builder, &mount_rules[0]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add first mount rule for iterator gap test");
    err = protecc_profile_builder_add_mount_rule(builder, &mount_rules[1]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add second mount rule for iterator gap test");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL,
                "Expected domain-only builder compile (no path patterns) to succeed");

    err = protecc_profile_export_net(compiled, NULL, 0, &net_export_size);
    TEST_ASSERT(err == PROTECC_OK && net_export_size > sizeof(protecc_net_profile_header_t),
                "Failed to query net export for multi-rule iterator test");
    net_buffer = malloc(net_export_size);
    TEST_ASSERT(net_buffer != NULL, "Failed to allocate net export buffer for iterator test");
    err = protecc_profile_export_net(compiled, net_buffer, net_export_size, &net_export_size);
    TEST_ASSERT(err == PROTECC_OK, "Failed to export net blob for iterator test");

    err = protecc_profile_export_mounts(compiled, NULL, 0, &mount_export_size);
    TEST_ASSERT(err == PROTECC_OK && mount_export_size > sizeof(protecc_mount_profile_header_t),
                "Failed to query mount export for multi-rule iterator test");
    mount_buffer = malloc(mount_export_size);
    TEST_ASSERT(mount_buffer != NULL, "Failed to allocate mount export buffer for iterator test");
    err = protecc_profile_export_mounts(compiled, mount_buffer, mount_export_size, &mount_export_size);
    TEST_ASSERT(err == PROTECC_OK, "Failed to export mount blob for iterator test");

    {
        protecc_net_blob_view_t net_view;
        protecc_net_rule_view_t net_rule;
        size_t net_iter = 0;

        err = protecc_profile_net_view_init(net_buffer, net_export_size, &net_view);
        TEST_ASSERT(err == PROTECC_OK && net_view.rule_count == 2,
                    "Expected 2-rule net view in iterator gap test");

        err = protecc_profile_net_view_first(&net_view, &net_iter, &net_rule);
        TEST_ASSERT(err == PROTECC_OK && net_iter == 0,
                    "Expected net first() to position at index 0 in iterator gap test");
        err = protecc_profile_net_view_next(&net_view, &net_iter, &net_rule);
        TEST_ASSERT(err == PROTECC_OK && net_iter == 1,
                    "Expected net next() to advance to index 1 in iterator gap test");
        err = protecc_profile_net_view_next(&net_view, &net_iter, &net_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected net next() to fail at iterator end in iterator gap test");

        err = protecc_profile_net_view_first(&net_view, NULL, &net_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected net first() with NULL iterator to fail");
        err = protecc_profile_net_view_next(&net_view, NULL, &net_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected net next() with NULL iterator to fail");
        err = protecc_profile_net_view_get_rule(&net_view, 0, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected net get_rule() with NULL output to fail");

        {
            protecc_net_profile_rule_t* raw_rules =
                (protecc_net_profile_rule_t*)(net_buffer + sizeof(protecc_net_profile_header_t));
            uint8_t saved_action = raw_rules[0].action;
            uint8_t saved_protocol = raw_rules[0].protocol;
            uint8_t saved_family = raw_rules[0].family;

            raw_rules[0].action = 0xFFu;
            err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_BLOB,
                        "Expected invalid net action to fail validation");
            raw_rules[0].action = saved_action;

            raw_rules[0].protocol = 0xFEu;
            err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_BLOB,
                        "Expected invalid net protocol to fail validation");
            raw_rules[0].protocol = saved_protocol;

            raw_rules[0].family = 0xFDu;
            err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_BLOB,
                        "Expected invalid net family to fail validation");
            raw_rules[0].family = saved_family;
        }
    }

    {
        protecc_mount_blob_view_t mount_view;
        protecc_mount_rule_view_t mount_rule;
        size_t mount_iter = 0;

        err = protecc_profile_mount_view_init(mount_buffer, mount_export_size, &mount_view);
        TEST_ASSERT(err == PROTECC_OK && mount_view.rule_count == 2,
                    "Expected 2-rule mount view in iterator gap test");

        err = protecc_profile_mount_view_first(&mount_view, &mount_iter, &mount_rule);
        TEST_ASSERT(err == PROTECC_OK && mount_iter == 0,
                    "Expected mount first() to position at index 0 in iterator gap test");
        err = protecc_profile_mount_view_next(&mount_view, &mount_iter, &mount_rule);
        TEST_ASSERT(err == PROTECC_OK && mount_iter == 1,
                    "Expected mount next() to advance to index 1 in iterator gap test");
        err = protecc_profile_mount_view_next(&mount_view, &mount_iter, &mount_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected mount next() to fail at iterator end in iterator gap test");

        err = protecc_profile_mount_view_first(&mount_view, NULL, &mount_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected mount first() with NULL iterator to fail");
        err = protecc_profile_mount_view_next(&mount_view, NULL, &mount_rule);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected mount next() with NULL iterator to fail");
        err = protecc_profile_mount_view_get_rule(&mount_view, 0, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected mount get_rule() with NULL output to fail");

        {
            protecc_mount_profile_rule_t* raw_rules =
                (protecc_mount_profile_rule_t*)(mount_buffer + sizeof(protecc_mount_profile_header_t));
            uint8_t saved_action = raw_rules[0].action;
            raw_rules[0].action = 0xEEu;
            err = protecc_profile_validate_mount_blob(mount_buffer, mount_export_size);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_BLOB,
                        "Expected invalid mount action to fail validation");
            raw_rules[0].action = saved_action;
        }
    }

    err = protecc_profile_import_net_blob(net_buffer, net_export_size, NULL, &net_export_size);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                "Expected net import with NULL rulesOut to fail");
    {
        protecc_net_rule_t* imported_net = NULL;
        err = protecc_profile_import_net_blob(net_buffer, net_export_size, &imported_net, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected net import with NULL countOut to fail");
    }

    err = protecc_profile_import_mount_blob(mount_buffer, mount_export_size, NULL, &mount_export_size);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                "Expected mount import with NULL rulesOut to fail");
    {
        protecc_mount_rule_t* imported_mount = NULL;
        err = protecc_profile_import_mount_blob(mount_buffer, mount_export_size, &imported_mount, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected mount import with NULL countOut to fail");
    }

    err = protecc_profile_builder_add_net_rule(NULL, &net_rules[0]);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_net_rule(NULL, ..) to fail");
    err = protecc_profile_builder_add_net_rule(builder, NULL);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_net_rule(.., NULL) to fail");

    err = protecc_profile_builder_add_mount_rule(NULL, &mount_rules[0]);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_mount_rule(NULL, ..) to fail");
    err = protecc_profile_builder_add_mount_rule(builder, NULL);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_mount_rule(.., NULL) to fail");

    {
        const protecc_pattern_t path_pattern[] = {{"/tmp/*", PROTECC_PERM_READ}};
        err = protecc_profile_builder_add_patterns(NULL, path_pattern, 1);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_patterns(NULL, ..) to fail");
        err = protecc_profile_builder_add_patterns(builder, NULL, 1);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_patterns(.., NULL) to fail");
        err = protecc_profile_builder_add_patterns(builder, path_pattern, 0);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT, "Expected add_patterns(.., count=0) to fail");
    }

    free(net_buffer);
    free(mount_buffer);
    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);
    return 0;
}