/**
 * @file test_profile_builder_net.c
 * @brief Runtime net matcher tests for protecc profile builder
 */

#include <protecc/protecc.h>
#include <stdio.h>

#include "../private.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

int test_profile_builder_runtime_net_matchers(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    protecc_action_t action = PROTECC_ACTION_ALLOW;

    protecc_net_rule_t net_rules[] = {
        {
            .action = PROTECC_ACTION_ALLOW,
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip_pattern = "10.0.*.*",
            .port_from = 80,
            .port_to = 443,
            .unix_path_pattern = NULL
        },
        {
            .action = PROTECC_ACTION_DENY,
            .protocol = PROTECC_NET_PROTOCOL_UDP,
            .family = PROTECC_NET_FAMILY_ANY,
            .ip_pattern = "*",
            .port_from = 53,
            .port_to = 53,
            .unix_path_pattern = NULL
        },
        {
            .action = PROTECC_ACTION_AUDIT,
            .protocol = PROTECC_NET_PROTOCOL_UNIX,
            .family = PROTECC_NET_FAMILY_UNIX,
            .ip_pattern = NULL,
            .port_from = 0,
            .port_to = 0,
            .unix_path_pattern = "/run/*.sock"
        }
    };

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder for net runtime matcher test");

    err = protecc_profile_builder_add_net_rule(builder, &net_rules[0]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add net rule 0 for runtime matcher test");
    err = protecc_profile_builder_add_net_rule(builder, &net_rules[1]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add net rule 1 for runtime matcher test");
    err = protecc_profile_builder_add_net_rule(builder, &net_rules[2]);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add net rule 2 for runtime matcher test");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL,
                "Failed to compile profile for net runtime matcher test");

    {
        protecc_net_request_t req = {
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip = "10.0.42.9",
            .port = 443,
            .unix_path = NULL
        };
        bool matched = protecc_match_net(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_ALLOW,
                    "Expected TCP IPv4 request to match ALLOW net rule");
    }

    {
        protecc_net_request_t req = {
            .protocol = PROTECC_NET_PROTOCOL_UDP,
            .family = PROTECC_NET_FAMILY_IPV6,
            .ip = "2001:db8::1",
            .port = 53,
            .unix_path = NULL
        };
        bool matched = protecc_match_net(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_DENY,
                    "Expected UDP DNS request to match DENY net rule");
    }

    {
        protecc_net_request_t req = {
            .protocol = PROTECC_NET_PROTOCOL_UNIX,
            .family = PROTECC_NET_FAMILY_UNIX,
            .ip = NULL,
            .port = 0,
            .unix_path = "/run/service.sock"
        };
        bool matched = protecc_match_net(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_AUDIT,
                    "Expected unix socket request to match AUDIT net rule");
    }

    {
        protecc_net_request_t req = {
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip = "192.168.1.10",
            .port = 22,
            .unix_path = NULL
        };
        bool matched = protecc_match_net(compiled, &req, &action);
        TEST_ASSERT(!matched, "Expected unmatched net request to return false");
    }

    TEST_ASSERT(!protecc_match_net(NULL, NULL, NULL),
                "Expected net matcher to fail on NULL input");

    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);

    {
        protecc_profile_builder_t* precedence_builder = protecc_profile_builder_create();
        protecc_profile_t* precedence_compiled = NULL;
        protecc_net_rule_t precedence_rules[] = {
            {
                .action = PROTECC_ACTION_DENY,
                .protocol = PROTECC_NET_PROTOCOL_TCP,
                .family = PROTECC_NET_FAMILY_IPV4,
                .ip_pattern = "10.10.*.*",
                .port_from = 443,
                .port_to = 443,
                .unix_path_pattern = NULL
            },
            {
                .action = PROTECC_ACTION_ALLOW,
                .protocol = PROTECC_NET_PROTOCOL_TCP,
                .family = PROTECC_NET_FAMILY_IPV4,
                .ip_pattern = "10.10.1.5",
                .port_from = 443,
                .port_to = 443,
                .unix_path_pattern = NULL
            }
        };

        TEST_ASSERT(precedence_builder != NULL, "Failed to create precedence net builder");
        err = protecc_profile_builder_add_net_rule(precedence_builder, &precedence_rules[0]);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add precedence net deny rule");
        err = protecc_profile_builder_add_net_rule(precedence_builder, &precedence_rules[1]);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add precedence net allow rule");
        err = protecc_profile_compile(precedence_builder, PROTECC_FLAG_NONE, NULL, &precedence_compiled);
        TEST_ASSERT(err == PROTECC_OK && precedence_compiled != NULL,
                    "Failed to compile precedence net profile");

        {
            protecc_net_request_t req = {
                .protocol = PROTECC_NET_PROTOCOL_TCP,
                .family = PROTECC_NET_FAMILY_IPV4,
                .ip = "10.10.1.5",
                .port = 443,
                .unix_path = NULL
            };
            bool matched = protecc_match_net(precedence_compiled, &req, &action);
            TEST_ASSERT(matched && action == PROTECC_ACTION_DENY,
                        "Expected first matching net rule to win (DENY)");
        }

        protecc_free(precedence_compiled);
        protecc_profile_builder_destroy(precedence_builder);
    }

    {
        protecc_profile_builder_t* explicit_builder = protecc_profile_builder_create();
        protecc_profile_t* explicit_compiled = NULL;
        protecc_net_rule_t explicit_rule = {
            .action = PROTECC_ACTION_ALLOW,
            .protocol = PROTECC_NET_PROTOCOL_UNIX,
            .family = PROTECC_NET_FAMILY_UNIX,
            .ip_pattern = NULL,
            .port_from = 0,
            .port_to = 0,
            .unix_path_pattern = "/[Rr][Uu][Nn]/[Ss]ervice.sock"
        };

        TEST_ASSERT(explicit_builder != NULL, "Failed to create explicit net builder");
        err = protecc_profile_builder_add_net_rule(explicit_builder, &explicit_rule);
        TEST_ASSERT(err == PROTECC_OK, "Failed to add explicit net rule");
        err = protecc_profile_compile(explicit_builder, PROTECC_FLAG_NONE, NULL, &explicit_compiled);
        TEST_ASSERT(err == PROTECC_OK && explicit_compiled != NULL,
                    "Failed to compile explicit net profile");

        {
            protecc_net_request_t req = {
                .protocol = PROTECC_NET_PROTOCOL_UNIX,
                .family = PROTECC_NET_FAMILY_UNIX,
                .ip = NULL,
                .port = 0,
                .unix_path = "/run/Service.sock"
            };
            bool matched = protecc_match_net(explicit_compiled, &req, &action);
            TEST_ASSERT(matched && action == PROTECC_ACTION_ALLOW,
                        "Expected explicit charset net pattern match");
        }

        protecc_free(explicit_compiled);
        protecc_profile_builder_destroy(explicit_builder);
    }

    return 0;
}

int test_profile_builder_net_dfa_without_linear_rules(void)
{
    protecc_profile_builder_t* builder = NULL;
    protecc_profile_t* compiled = NULL;
    protecc_error_t err;
    protecc_action_t action = PROTECC_ACTION_DENY;
    size_t saved_count;

    protecc_net_rule_t net_rule = {
        .action = PROTECC_ACTION_ALLOW,
        .protocol = PROTECC_NET_PROTOCOL_TCP,
        .family = PROTECC_NET_FAMILY_IPV4,
        .ip_pattern = "192.168.50.*",
        .port_from = 8080,
        .port_to = 8080,
        .unix_path_pattern = NULL
    };

    builder = protecc_profile_builder_create();
    TEST_ASSERT(builder != NULL, "Failed to create profile builder for DFA-only net test");

    err = protecc_profile_builder_add_net_rule(builder, &net_rule);
    TEST_ASSERT(err == PROTECC_OK, "Failed to add net rule for DFA-only net test");

    err = protecc_profile_compile(builder, PROTECC_FLAG_NONE, NULL, &compiled);
    TEST_ASSERT(err == PROTECC_OK && compiled != NULL, "Failed to compile profile for DFA-only net test");
    TEST_ASSERT(compiled->net_ip_dfa != NULL && compiled->net_ip_dfa->present,
                "Expected net IP DFA to be present");

    saved_count = compiled->net_rule_count;
    compiled->net_rule_count = 0; /* Disable linear fallback; DFA must succeed */

    {
        protecc_net_request_t req = {
            .protocol = PROTECC_NET_PROTOCOL_TCP,
            .family = PROTECC_NET_FAMILY_IPV4,
            .ip = "192.168.50.25",
            .port = 8080,
            .unix_path = NULL
        };

        bool matched = protecc_match_net(compiled, &req, &action);
        TEST_ASSERT(matched && action == PROTECC_ACTION_ALLOW,
                    "Expected DFA match to succeed with linear rules disabled");
    }

    compiled->net_rule_count = saved_count; /* restore for cleanup */

    protecc_free(compiled);
    protecc_profile_builder_destroy(builder);
    return 0;
}
