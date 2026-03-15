/**
 * @file test_profile_builder_paths.c
 * @brief Path-centric profile builder tests for protecc library
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

int test_profile_builder_paths_and_reset(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;

    const protecc_pattern_t first_patterns[] = {
        {"/etc/*", PROTECC_PERM_READ},
        {"/var/log/**", PROTECC_PERM_READ | PROTECC_PERM_WRITE}
    };
    const protecc_pattern_t reset_patterns[] = {
        {"/tmp/new", PROTECC_PERM_EXECUTE}
    };

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder");

    err = protecc_profile_builder_add_patterns(builder, first_patterns, 2);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add path patterns");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK, "Failed to compile profile builder with paths");
    TEST_ASSERT(compiled != NULL, "Compiled profile is NULL");

    TEST_ASSERT(protecc_match_path(compiled, "/etc/hosts", PROTECC_PERM_READ),
                "Expected /etc/hosts to match with READ perms");
    TEST_ASSERT(protecc_match_path(compiled, "/var/log/app/x.log", PROTECC_PERM_READ | PROTECC_PERM_WRITE),
                "Expected /var/log path to match with READ|WRITE perms");

    protecc_free(compiled);
    compiled = NULL;

    protecc_profile_builder_reset(builder);
    err = protecc_profile_builder_add_patterns(builder, reset_patterns, 1);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add pattern after reset");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK, "Failed to compile profile after reset");

    TEST_ASSERT(protecc_match_path(compiled, "/tmp/new", PROTECC_PERM_EXECUTE),
                "Expected /tmp/new to match after reset");

    {
        bool matched = protecc_match_path(compiled, "/etc/hosts", PROTECC_PERM_NONE);
        TEST_ASSERT(!matched, "Old pattern should not match after reset");
    }

    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);
    return 0;
}

int test_profile_builder_invalid_rule_rejection(void)
{
    protecc_profile_builder_t* builder = protecc_profile_builder_create();
    protecc_error_t err;

    protecc_net_rule_t invalid_net = {
        .action = PROTECC_ACTION_ALLOW,
        .protocol = PROTECC_NET_PROTOCOL_UNIX,
        .family = PROTECC_NET_FAMILY_IPV4,
        .ip_pattern = NULL,
        .port_from = 0,
        .port_to = 0,
        .unix_path_pattern = "/tmp/sock"
    };

    protecc_mount_rule_t invalid_mount = {
        .action = PROTECC_ACTION_ALLOW,
        .source_pattern = "[bad",
        .target_pattern = "/mnt/*",
        .fstype_pattern = "ext4",
        .options_pattern = "rw",
        .flags = 0
    };

    TEST_ASSERT(builder != NULL, "Failed to create profile builder");

    err = protecc_profile_builder_add_net_rule(builder, &invalid_net);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                "Expected invalid unix net rule to be rejected");

    err = protecc_profile_builder_add_mount_rule(builder, &invalid_mount);
    TEST_ASSERT(err == PROTECC_ERROR_INVALID_PATTERN,
                "Expected invalid mount source pattern to be rejected");

    protecc_profile_builder_destroy(builder);
    return 0;
}

int test_profile_builder_path_permission_precedence(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    const protecc_pattern_t patterns[] = {
        {"/opt/**", PROTECC_PERM_READ},
        {"/opt/app/*", PROTECC_PERM_WRITE},
        {"/data/*", PROTECC_PERM_READ},
        {"/data/?", PROTECC_PERM_WRITE}
    };

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder");

    err = protecc_profile_builder_add_patterns(builder, patterns, 4);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add precedence path patterns");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL,
                "Failed to compile path precedence profile");

    TEST_ASSERT(protecc_match_path(compiled, "/opt/app/tool", PROTECC_PERM_WRITE),
                "Expected deeper /opt/app/* rule to grant WRITE");
    TEST_ASSERT(!protecc_match_path(compiled, "/opt/app/tool", PROTECC_PERM_READ),
                "Expected deeper /opt/app/* rule to override less specific READ-only rule");

    TEST_ASSERT(protecc_match_path(compiled, "/data/x", PROTECC_PERM_READ | PROTECC_PERM_WRITE),
                "Expected equal-depth matches to merge READ|WRITE permissions");
    TEST_ASSERT(!protecc_match_path(compiled, "/data/x", PROTECC_PERM_EXECUTE),
                "Expected merged permissions to exclude EXECUTE");

    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);
    return 0;
}