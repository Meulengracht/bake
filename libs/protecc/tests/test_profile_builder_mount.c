/**
 * @file test_profile_builder_mount.c
 * @brief Runtime mount matcher tests for protecc profile builder
 */

#include <protecc/protecc.h>
#include <stdio.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

int test_profile_builder_runtime_mount_matchers(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    protecc_action_t action = PROTECC_ACTION_ALLOW;

    protecc_mount_rule_t mount_rules[] = {
        {
            .action = PROTECC_ACTION_ALLOW,
            .source_pattern = "/dev/sd*",
            .target_pattern = "/mnt/*",
            .fstype_pattern = "ext4",
            .options_pattern = "rw*",
            .flags = 0x1
        },
        {
            .action = PROTECC_ACTION_DENY,
            .source_pattern = NULL,
            .target_pattern = "/mnt/private/*",
            .fstype_pattern = NULL,
            .options_pattern = NULL,
            .flags = 0
        }
    };

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder for mount runtime matcher test");

    err = protecc_profile_builder_add_mount_rule(builder, &mount_rules[0]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add mount rule 0 for runtime matcher test");
    err = protecc_profile_builder_add_mount_rule(builder, &mount_rules[1]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add mount rule 1 for runtime matcher test");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL,
                "Failed to compile profile for mount runtime matcher test");

    {
        protecc_mount_request_t req = {
            .source = "/dev/sda1",
            .target = "/mnt/data",
            .fstype = "ext4",
            .options = "rw,nosuid",
            .flags = 0x1
        };
        bool matched = protecc_match_mount(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_ALLOW,
                    "Expected mount request to match ALLOW mount rule");
    }

    {
        protecc_mount_request_t req = {
            .source = "/dev/sda2",
            .target = "/mnt/private/secrets",
            .fstype = "ext4",
            .options = "ro",
            .flags = 0
        };
        bool matched = protecc_match_mount(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_DENY,
                    "Expected private mount target to match DENY mount rule");
    }

    {
        protecc_mount_request_t req = {
            .source = "/dev/sda1",
            .target = "/mnt/data",
            .fstype = "ext4",
            .options = "rw",
            .flags = 0
        };
        bool matched = protecc_match_mount(compiled, &req, &action);
        TEST_ASSERT(!matched, "Expected flag-mismatch mount request to return false");
    }

    {
        protecc_mount_request_t req = {
            .source = "/dev/loop0",
            .target = "/mnt/other",
            .fstype = "xfs",
            .options = "rw",
            .flags = 0
        };
        bool matched = protecc_match_mount(compiled, &req, NULL);
        TEST_ASSERT(!matched, "Expected unmatched mount request to return false");
    }

    TEST_ASSERT(!protecc_match_mount(NULL, NULL, NULL),
                "Expected mount matcher to fail on NULL input");

    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);

    {
        protecc_profile_builder_t* precedence_builder = protecc_profile_builder_create();
        protecc_profile_t* precedence_compiled = NULL;
        protecc_mount_rule_t precedence_rules[] = {
            {
                .action = PROTECC_ACTION_DENY,
                .source_pattern = "/dev/*",
                .target_pattern = "/mnt/*",
                .fstype_pattern = NULL,
                .options_pattern = NULL,
                .flags = 0
            },
            {
                .action = PROTECC_ACTION_ALLOW,
                .source_pattern = "/dev/sda1",
                .target_pattern = "/mnt/data",
                .fstype_pattern = NULL,
                .options_pattern = NULL,
                .flags = 0
            }
        };

        TEST_ASSERT(precedence_builder != NULL, "Failed to create precedence mount builder");
        err = protecc_profile_builder_add_mount_rule(precedence_builder, &precedence_rules[0]);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add precedence mount deny rule");
        err = protecc_profile_builder_add_mount_rule(precedence_builder, &precedence_rules[1]);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add precedence mount allow rule");
        err = protecc_profile_compile(precedence_builder, PROTECC_FLAG_NONE, NULL, &precedence_compiled);
        TEST_ASSERT(err == PROTECC_OK && precedence_compiled != NULL,
                    "Failed to compile precedence mount profile");

        {
            protecc_mount_request_t req = {
                .source = "/dev/sda1",
                .target = "/mnt/data",
                .fstype = "ext4",
                .options = "rw",
                .flags = 0
            };
            bool matched = protecc_match_mount(precedence_compiled, &req, &action);
            TEST_ASSERT(matched && action == PROTECC_ACTION_DENY,
                        "Expected first matching mount rule to win (DENY)");
        }

        protecc_free(precedence_compiled);
        protecc_profile_builder_destroy(precedence_builder);
    }

    {
        protecc_profile_builder_t* ci_builder = protecc_profile_builder_create();
        protecc_profile_t* ci_compiled = NULL;
        protecc_mount_rule_t ci_rule = {
            .action = PROTECC_ACTION_ALLOW,
            .source_pattern = "/DEV/SDA?",
            .target_pattern = "/MNT/[dD]ata",
            .fstype_pattern = "EXT*",
            .options_pattern = "RW*",
            .flags = 0
        };

        TEST_ASSERT(ci_builder != NULL, "Failed to create case-insensitive mount builder");
        err = protecc_profile_builder_add_mount_rule(ci_builder, &ci_rule);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add case-insensitive mount rule");
        err = protecc_profile_compile(ci_builder, PROTECC_FLAG_CASE_INSENSITIVE, NULL, &ci_compiled);
        TEST_ASSERT(err == PROTECC_OK && ci_compiled != NULL,
                    "Failed to compile case-insensitive mount profile");

        {
            protecc_mount_request_t req = {
                .source = "/dev/sda1",
                .target = "/mnt/data",
                .fstype = "ext4",
                .options = "rw,nosuid",
                .flags = 0
            };
            bool matched = protecc_match_mount(ci_compiled, &req, &action);
            TEST_ASSERT(matched && action == PROTECC_ACTION_ALLOW,
                        "Expected case-insensitive mount glob/charset match");
        }

        protecc_free(ci_compiled);
        protecc_profile_builder_destroy(ci_builder);
    }

    return 0;
}
