/**
 * @file test_profile_builder.c
 * @brief Profile builder tests for protecc library
 */

#include <protecc/protecc.h>
#include <protecc/profile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

static bool match_path_with_perms(
    const protecc_compiled_t* compiled,
    const char*               path,
    protecc_permission_t      expected)
{
    protecc_permission_t perms = PROTECC_PERM_NONE;

    if (!protecc_match(compiled, path, 0, &perms)) {
        return false;
    }

    return perms == expected;
}

int test_profile_builder(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder");

    {
        const protecc_pattern_t patterns[] = {
            {"/etc/*", PROTECC_PERM_READ},
            {"/var/log/**", PROTECC_PERM_READ | PROTECC_PERM_WRITE}
        };

        err = protecc_profile_builder_add_patterns(builder, patterns, 2);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add path patterns");

        err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile profile builder with paths");
        TEST_ASSERT(compiled != NULL, "Compiled profile is NULL");

        TEST_ASSERT(match_path_with_perms(compiled, "/etc/hosts", PROTECC_PERM_READ),
                    "Expected /etc/hosts to match with READ perms");
        TEST_ASSERT(match_path_with_perms(compiled, "/var/log/app/x.log", PROTECC_PERM_READ | PROTECC_PERM_WRITE),
                    "Expected /var/log path to match with READ|WRITE perms");

        protecc_free(compiled);
        compiled = NULL;
    }

    {
        const protecc_pattern_t patterns[] = {
            {"/tmp/new", PROTECC_PERM_EXECUTE}
        };

        protecc_profile_builder_reset(builder);

        err = protecc_profile_builder_add_patterns(builder, patterns, 1);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add pattern after reset");

        err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK, "Failed to compile profile after reset");

        TEST_ASSERT(match_path_with_perms(compiled, "/tmp/new", PROTECC_PERM_EXECUTE),
                    "Expected /tmp/new to match after reset");

        {
            protecc_permission_t perms = PROTECC_PERM_NONE;
            bool matched = protecc_match(compiled, "/etc/hosts", 0, &perms);
            TEST_ASSERT(!matched, "Old pattern should not match after reset");
        }

        protecc_free(compiled);
        compiled = NULL;
    }

    {
        protecc_net_rule_t net_rule = {
            .action = PROTECC_ACTION_ALLOW,
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip_pattern = "10.0.*.*",
            .port_from = 80,
            .port_to = 443,
            .unix_path_pattern = NULL
        };

        err = protecc_profile_builder_add_net_rule(builder, &net_rule);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add valid net rule");

        err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
        TEST_ASSERT(err == PROTECC_OK,
                    "Expected net rules to compile in phase 3");

        {
            size_t net_export_size = 0;
            uint8_t* net_buffer = NULL;
            const protecc_net_profile_header_t* net_header = NULL;

            err = protecc_profile_export_net(compiled, NULL, 0, &net_export_size);
            TEST_ASSERT(err == PROTECC_OK && net_export_size >= sizeof(protecc_net_profile_header_t),
                        "Failed to query net export size");

            net_buffer = malloc(net_export_size);
            TEST_ASSERT(net_buffer != NULL, "Failed to allocate net export buffer");

            err = protecc_profile_export_net(compiled, net_buffer, net_export_size, &net_export_size);
            TEST_ASSERT(err == PROTECC_OK, "Failed to export net profile");

            err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
            TEST_ASSERT(err == PROTECC_OK, "Expected exported net profile to validate");

            net_header = (const protecc_net_profile_header_t*)net_buffer;
            TEST_ASSERT(net_header->magic == PROTECC_NET_PROFILE_MAGIC,
                        "Unexpected net profile magic");
            TEST_ASSERT(net_header->version == PROTECC_NET_PROFILE_VERSION,
                        "Unexpected net profile version");
            TEST_ASSERT(net_header->rule_count == 1,
                        "Expected one net rule in exported profile");

            {
                protecc_net_rule_t* imported = NULL;
                size_t imported_count = 0;

                err = protecc_profile_import_net_blob(net_buffer, net_export_size, &imported, &imported_count);
                TEST_ASSERT(err == PROTECC_OK, "Failed to import net blob");
                TEST_ASSERT(imported_count == 1, "Expected one imported net rule");
                TEST_ASSERT(imported[0].action == PROTECC_ACTION_ALLOW, "Unexpected imported net action");
                TEST_ASSERT(imported[0].protocol == PROTECC_NET_PROTOCOL_TCP, "Unexpected imported net protocol");
                TEST_ASSERT(imported[0].family == PROTECC_NET_FAMILY_IPV4, "Unexpected imported net family");
                TEST_ASSERT(imported[0].port_from == 80 && imported[0].port_to == 443,
                            "Unexpected imported net port range");
                TEST_ASSERT(imported[0].ip_pattern != NULL && strcmp(imported[0].ip_pattern, "10.0.*.*") == 0,
                            "Unexpected imported net ip pattern");

                protecc_profile_free_net_rules(imported, imported_count);
            }

            {
                protecc_net_blob_view_t view;
                protecc_net_rule_view_t rule;
                size_t iter_index = 0;

                err = protecc_profile_net_view_init(net_buffer, net_export_size, &view);
                TEST_ASSERT(err == PROTECC_OK, "Failed to init net zero-copy view");
                TEST_ASSERT(view.rule_count == 1, "Expected one rule in net zero-copy view");

                err = protecc_profile_net_view_get_rule(&view, 0, &rule);
                TEST_ASSERT(err == PROTECC_OK, "Failed to decode first net zero-copy rule");
                TEST_ASSERT(rule.protocol == PROTECC_NET_PROTOCOL_TCP, "Unexpected net view protocol");
                TEST_ASSERT(rule.ip_pattern != NULL && strcmp(rule.ip_pattern, "10.0.*.*") == 0,
                            "Unexpected net view ip pattern");

                err = protecc_profile_net_view_first(&view, &iter_index, &rule);
                TEST_ASSERT(err == PROTECC_OK && iter_index == 0,
                            "Expected net iterator first() to point at first rule");
                TEST_ASSERT(rule.ip_pattern != NULL && strcmp(rule.ip_pattern, "10.0.*.*") == 0,
                            "Unexpected net iterator first rule contents");

                err = protecc_profile_net_view_next(&view, &iter_index, &rule);
                TEST_ASSERT(err != PROTECC_OK,
                            "Expected net iterator next() to fail at end for single-rule profile");

                err = protecc_profile_net_view_get_rule(&view, 1, &rule);
                TEST_ASSERT(err != PROTECC_OK, "Expected out-of-range net view index to fail");
            }

            {
                uint8_t saved_magic_byte = net_buffer[0];
                net_buffer[0] ^= 0xFFu;
                err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
                TEST_ASSERT(err != PROTECC_OK, "Expected invalid net magic to fail validation");
                net_buffer[0] = saved_magic_byte;
            }

            free(net_buffer);
        }

        protecc_free(compiled);
        compiled = NULL;
    }

    {
        protecc_net_rule_t invalid_net = {
            .action = PROTECC_ACTION_ALLOW,
            .protocol = PROTECC_NET_PROTOCOL_UNIX,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip_pattern = NULL,
            .port_from = 0,
            .port_to = 0,
            .unix_path_pattern = "/tmp/sock"
        };

        err = protecc_profile_builder_add_net_rule(builder, &invalid_net);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected invalid unix net rule to be rejected");
    }

    {
        protecc_mount_rule_t invalid_mount = {
            .action = PROTECC_ACTION_ALLOW,
            .source_pattern = "[bad",
            .target_pattern = "/mnt/*",
            .fstype_pattern = "ext4",
            .options_pattern = "rw",
            .flags = 0
        };

        err = protecc_profile_builder_add_mount_rule(builder, &invalid_mount);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_PATTERN,
                    "Expected invalid mount source pattern to be rejected");
    }

    {
        protecc_profile_builder_reset(builder);

        {
            const protecc_pattern_t patterns[] = {
                {"/opt/**", PROTECC_PERM_READ}
            };
            protecc_mount_rule_t mount_rule = {
                .action = PROTECC_ACTION_ALLOW,
                .source_pattern = "/dev/sd*",
                .target_pattern = "/mnt/*",
                .fstype_pattern = "ext4",
                .options_pattern = "rw",
                .flags = 0
            };
            size_t export_size = 0;
            size_t export_path_size = 0;
            void* buffer_a = NULL;
            void* buffer_b = NULL;

            err = protecc_profile_builder_add_patterns(builder, patterns, 1);
            TEST_ASSERT(err == PROTECC_OK, "Failed to add path pattern for export test");

            err = protecc_profile_builder_add_mount_pattern(builder, &mount_rule);
            TEST_ASSERT(err == PROTECC_OK, "Failed to add valid mount rule via alias");

            err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
            TEST_ASSERT(err == PROTECC_OK,
                        "Expected mount rules to compile in phase 3");

            {
                size_t mount_export_size = 0;
                uint8_t* mount_buffer = NULL;
                const protecc_mount_profile_header_t* mount_header = NULL;

                err = protecc_profile_export_mounts(compiled, NULL, 0, &mount_export_size);
                TEST_ASSERT(err == PROTECC_OK && mount_export_size >= sizeof(protecc_mount_profile_header_t),
                            "Failed to query mount export size");

                mount_buffer = malloc(mount_export_size);
                TEST_ASSERT(mount_buffer != NULL, "Failed to allocate mount export buffer");

                err = protecc_profile_export_mounts(compiled, mount_buffer, mount_export_size, &mount_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Failed to export mount profile");

                err = protecc_profile_validate_mount_blob(mount_buffer, mount_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Expected exported mount profile to validate");

                mount_header = (const protecc_mount_profile_header_t*)mount_buffer;
                TEST_ASSERT(mount_header->magic == PROTECC_MOUNT_PROFILE_MAGIC,
                            "Unexpected mount profile magic");
                TEST_ASSERT(mount_header->version == PROTECC_MOUNT_PROFILE_VERSION,
                            "Unexpected mount profile version");
                TEST_ASSERT(mount_header->rule_count == 1,
                            "Expected one mount rule in exported profile");

                {
                    protecc_mount_rule_t* imported = NULL;
                    size_t imported_count = 0;

                    err = protecc_profile_import_mount_blob(mount_buffer, mount_export_size, &imported, &imported_count);
                    TEST_ASSERT(err == PROTECC_OK, "Failed to import mount blob");
                    TEST_ASSERT(imported_count == 1, "Expected one imported mount rule");
                    TEST_ASSERT(imported[0].action == PROTECC_ACTION_ALLOW, "Unexpected imported mount action");
                    TEST_ASSERT(imported[0].source_pattern != NULL && strcmp(imported[0].source_pattern, "/dev/sd*") == 0,
                                "Unexpected imported mount source pattern");
                    TEST_ASSERT(imported[0].target_pattern != NULL && strcmp(imported[0].target_pattern, "/mnt/*") == 0,
                                "Unexpected imported mount target pattern");

                    protecc_profile_free_mount_rules(imported, imported_count);
                }

                {
                    protecc_mount_blob_view_t view;
                    protecc_mount_rule_view_t rule;
                    size_t iter_index = 0;

                    err = protecc_profile_mount_view_init(mount_buffer, mount_export_size, &view);
                    TEST_ASSERT(err == PROTECC_OK, "Failed to init mount zero-copy view");
                    TEST_ASSERT(view.rule_count == 1, "Expected one rule in mount zero-copy view");

                    err = protecc_profile_mount_view_get_rule(&view, 0, &rule);
                    TEST_ASSERT(err == PROTECC_OK, "Failed to decode first mount zero-copy rule");
                    TEST_ASSERT(rule.source_pattern != NULL && strcmp(rule.source_pattern, "/dev/sd*") == 0,
                                "Unexpected mount view source pattern");

                    err = protecc_profile_mount_view_first(&view, &iter_index, &rule);
                    TEST_ASSERT(err == PROTECC_OK && iter_index == 0,
                                "Expected mount iterator first() to point at first rule");
                    TEST_ASSERT(rule.source_pattern != NULL && strcmp(rule.source_pattern, "/dev/sd*") == 0,
                                "Unexpected mount iterator first rule contents");

                    err = protecc_profile_mount_view_next(&view, &iter_index, &rule);
                    TEST_ASSERT(err != PROTECC_OK,
                                "Expected mount iterator next() to fail at end for single-rule profile");

                    err = protecc_profile_mount_view_get_rule(&view, 1, &rule);
                    TEST_ASSERT(err != PROTECC_OK, "Expected out-of-range mount view index to fail");
                }

                {
                    protecc_mount_profile_rule_t* rules =
                        (protecc_mount_profile_rule_t*)(mount_buffer + sizeof(protecc_mount_profile_header_t));
                    uint32_t saved_off = rules[0].source_pattern_off;
                    rules[0].source_pattern_off = mount_header->strings_size + 1u;
                    err = protecc_profile_validate_mount_blob(mount_buffer, mount_export_size);
                    TEST_ASSERT(err != PROTECC_OK, "Expected invalid mount string offset to fail validation");
                    rules[0].source_pattern_off = saved_off;
                }

                free(mount_buffer);
            }

            protecc_free(compiled);
            compiled = NULL;

            protecc_profile_builder_reset(builder);
            err = protecc_profile_builder_add_patterns(builder, patterns, 1);
            TEST_ASSERT(err == PROTECC_OK, "Failed to add path pattern for export path test");

            err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
            TEST_ASSERT(err == PROTECC_OK, "Failed to compile pure path builder");

            err = protecc_export(compiled, NULL, 0, &export_size);
            TEST_ASSERT(err == PROTECC_OK && export_size > 0,
                        "Failed to query legacy export size");

            err = protecc_profile_export_path(compiled, NULL, 0, &export_path_size);
            TEST_ASSERT(err == PROTECC_OK && export_path_size == export_size,
                        "Expected path export size to match legacy export");

            buffer_a = malloc(export_size);
            buffer_b = malloc(export_path_size);
            TEST_ASSERT(buffer_a != NULL && buffer_b != NULL, "Failed to allocate export buffers");

            err = protecc_export(compiled, buffer_a, export_size, &export_size);
            TEST_ASSERT(err == PROTECC_OK, "Failed to run legacy export");

            err = protecc_profile_export_path(compiled, buffer_b, export_path_size, &export_path_size);
            TEST_ASSERT(err == PROTECC_OK, "Failed to run path-only export");

            TEST_ASSERT(memcmp(buffer_a, buffer_b, export_size) == 0,
                        "Expected path-only export bytes to match legacy export");

            {
                size_t net_export_size = 0;
                size_t mount_export_size = 0;
                uint8_t* net_buffer = NULL;
                uint8_t* mount_buffer = NULL;
                const protecc_net_profile_header_t* net_header = NULL;
                const protecc_mount_profile_header_t* mount_header = NULL;

                err = protecc_profile_export_net(compiled, NULL, 0, &net_export_size);
                TEST_ASSERT(err == PROTECC_OK && net_export_size == sizeof(protecc_net_profile_header_t),
                            "Expected empty net export to contain header only");

                err = protecc_profile_export_mounts(compiled, NULL, 0, &mount_export_size);
                TEST_ASSERT(err == PROTECC_OK && mount_export_size == sizeof(protecc_mount_profile_header_t),
                            "Expected empty mount export to contain header only");

                net_buffer = malloc(net_export_size);
                mount_buffer = malloc(mount_export_size);
                TEST_ASSERT(net_buffer != NULL && mount_buffer != NULL,
                            "Failed to allocate empty domain export buffers");

                err = protecc_profile_export_net(compiled, net_buffer, net_export_size, &net_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Failed to export empty net profile");

                err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Expected empty net blob to validate");

                err = protecc_profile_export_mounts(compiled, mount_buffer, mount_export_size, &mount_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Failed to export empty mount profile");

                err = protecc_profile_validate_mount_blob(mount_buffer, mount_export_size);
                TEST_ASSERT(err == PROTECC_OK, "Expected empty mount blob to validate");

                net_header = (const protecc_net_profile_header_t*)net_buffer;
                mount_header = (const protecc_mount_profile_header_t*)mount_buffer;

                TEST_ASSERT(net_header->rule_count == 0, "Expected zero rules in empty net export");
                TEST_ASSERT(mount_header->rule_count == 0, "Expected zero rules in empty mount export");

                {
                    protecc_net_rule_t* imported_net = NULL;
                    protecc_mount_rule_t* imported_mount = NULL;
                    size_t imported_net_count = 0;
                    size_t imported_mount_count = 0;

                    err = protecc_profile_import_net_blob(net_buffer, net_export_size, &imported_net, &imported_net_count);
                    TEST_ASSERT(err == PROTECC_OK, "Failed to import empty net blob");
                    TEST_ASSERT(imported_net == NULL && imported_net_count == 0,
                                "Expected empty net import to return no rules");

                    err = protecc_profile_import_mount_blob(mount_buffer, mount_export_size, &imported_mount, &imported_mount_count);
                    TEST_ASSERT(err == PROTECC_OK, "Failed to import empty mount blob");
                    TEST_ASSERT(imported_mount == NULL && imported_mount_count == 0,
                                "Expected empty mount import to return no rules");

                    protecc_profile_free_net_rules(imported_net, imported_net_count);
                    protecc_profile_free_mount_rules(imported_mount, imported_mount_count);
                }

                {
                    protecc_net_blob_view_t net_view;
                    protecc_mount_blob_view_t mount_view;
                    protecc_net_rule_view_t net_rule;
                    protecc_mount_rule_view_t mount_rule;
                    size_t iter_index = 0;

                    err = protecc_profile_net_view_init(net_buffer, net_export_size, &net_view);
                    TEST_ASSERT(err == PROTECC_OK && net_view.rule_count == 0,
                                "Expected empty net view with zero rules");

                    err = protecc_profile_mount_view_init(mount_buffer, mount_export_size, &mount_view);
                    TEST_ASSERT(err == PROTECC_OK && mount_view.rule_count == 0,
                                "Expected empty mount view with zero rules");

                    err = protecc_profile_net_view_get_rule(&net_view, 0, &net_rule);
                    TEST_ASSERT(err != PROTECC_OK, "Expected empty net view index access to fail");

                    err = protecc_profile_mount_view_get_rule(&mount_view, 0, &mount_rule);
                    TEST_ASSERT(err != PROTECC_OK, "Expected empty mount view index access to fail");

                    err = protecc_profile_net_view_first(&net_view, &iter_index, &net_rule);
                    TEST_ASSERT(err != PROTECC_OK, "Expected empty net iterator first() to fail");

                    err = protecc_profile_mount_view_first(&mount_view, &iter_index, &mount_rule);
                    TEST_ASSERT(err != PROTECC_OK, "Expected empty mount iterator first() to fail");
                }

                free(net_buffer);
                free(mount_buffer);
            }

            free(buffer_a);
            free(buffer_b);
            protecc_free(compiled);
            compiled = NULL;
        }
    }

    {
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

        protecc_profile_builder_reset(builder);

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
            TEST_ASSERT(net_rule.protocol == PROTECC_NET_PROTOCOL_TCP,
                        "Unexpected protocol for first net rule in iterator gap test");

            err = protecc_profile_net_view_next(&net_view, &net_iter, &net_rule);
            TEST_ASSERT(err == PROTECC_OK && net_iter == 1,
                        "Expected net next() to advance to index 1 in iterator gap test");
            TEST_ASSERT(net_rule.protocol == PROTECC_NET_PROTOCOL_UDP,
                        "Unexpected protocol for second net rule in iterator gap test");

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
            err = protecc_profile_net_view_first(&net_view, &net_iter, NULL);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected net first() with NULL output to fail");
            err = protecc_profile_net_view_next(&net_view, &net_iter, NULL);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected net next() with NULL output to fail");

            {
                protecc_net_profile_rule_t* raw_rules =
                    (protecc_net_profile_rule_t*)(net_buffer + sizeof(protecc_net_profile_header_t));
                uint8_t saved_action = raw_rules[0].action;
                uint8_t saved_protocol = raw_rules[0].protocol;
                uint8_t saved_family = raw_rules[0].family;

                raw_rules[0].action = 0xFFu;
                err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
                TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                            "Expected invalid net action to fail validation");
                raw_rules[0].action = saved_action;

                raw_rules[0].protocol = 0xFEu;
                err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
                TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                            "Expected invalid net protocol to fail validation");
                raw_rules[0].protocol = saved_protocol;

                raw_rules[0].family = 0xFDu;
                err = protecc_profile_validate_net_blob(net_buffer, net_export_size);
                TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
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
            TEST_ASSERT(mount_rule.flags == 1,
                        "Unexpected flags for first mount rule in iterator gap test");

            err = protecc_profile_mount_view_next(&mount_view, &mount_iter, &mount_rule);
            TEST_ASSERT(err == PROTECC_OK && mount_iter == 1,
                        "Expected mount next() to advance to index 1 in iterator gap test");
            TEST_ASSERT(mount_rule.flags == 2,
                        "Unexpected flags for second mount rule in iterator gap test");

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
            err = protecc_profile_mount_view_first(&mount_view, &mount_iter, NULL);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected mount first() with NULL output to fail");
            err = protecc_profile_mount_view_next(&mount_view, &mount_iter, NULL);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected mount next() with NULL output to fail");

            {
                protecc_mount_profile_rule_t* raw_rules =
                    (protecc_mount_profile_rule_t*)(mount_buffer + sizeof(protecc_mount_profile_header_t));
                uint8_t saved_action = raw_rules[0].action;

                raw_rules[0].action = 0xEEu;
                err = protecc_profile_validate_mount_blob(mount_buffer, mount_export_size);
                TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
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
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected add_net_rule(NULL, ..) to fail");
        err = protecc_profile_builder_add_net_rule(builder, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected add_net_rule(.., NULL) to fail");

        err = protecc_profile_builder_add_mount_rule(NULL, &mount_rules[0]);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected add_mount_rule(NULL, ..) to fail");
        err = protecc_profile_builder_add_mount_rule(builder, NULL);
        TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                    "Expected add_mount_rule(.., NULL) to fail");

        {
            const protecc_pattern_t path_pattern[] = {
                {"/tmp/*", PROTECC_PERM_READ}
            };
            err = protecc_profile_builder_add_patterns(NULL, path_pattern, 1);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected add_patterns(NULL, ..) to fail");
            err = protecc_profile_builder_add_patterns(builder, NULL, 1);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected add_patterns(.., NULL) to fail");
            err = protecc_profile_builder_add_patterns(builder, path_pattern, 0);
            TEST_ASSERT(err == PROTECC_ERROR_INVALID_ARGUMENT,
                        "Expected add_patterns(.., count=0) to fail");
        }

        free(net_buffer);
        free(mount_buffer);
        protecc_free(compiled);
        compiled = NULL;
    }

    protecc_profile_builder_destroy(builder);
    return 0;
}
