/**
 * @file test_profile_builder.c
 * @brief Profile builder tests for protecc library
 */

#include <protecc/protecc.h>
#include <stdio.h>

typedef int (*profile_builder_subtest_fn_t)(void);

extern int test_profile_builder_paths_and_reset(void);
extern int test_profile_builder_invalid_rule_rejection(void);
extern int test_profile_builder_path_permission_precedence(void);
extern int test_profile_builder_net_single_rule(void);
extern int test_profile_builder_mount_path_exports_and_empty_domains(void);
extern int test_profile_builder_domain_only_and_negatives(void);

int test_profile_builder(void)
{
    static const struct {
        const char* name;
        profile_builder_subtest_fn_t fn;
    } subtests[] = {
        {"paths_and_reset", test_profile_builder_paths_and_reset},
        {"net_single_rule", test_profile_builder_net_single_rule},
        {"invalid_rule_rejection", test_profile_builder_invalid_rule_rejection},
        {"path_permission_precedence", test_profile_builder_path_permission_precedence},
        {"mount_path_and_empty_domains", test_profile_builder_mount_path_exports_and_empty_domains},
        {"domain_only_and_negatives", test_profile_builder_domain_only_and_negatives},
    };

    for (size_t i = 0; i < sizeof(subtests) / sizeof(subtests[0]); i++) {
        int result = subtests[i].fn();
        if (result != 0) {
            printf("  Subtest failed: %s\n", subtests[i].name);
            return result;
        }
    }

    return 0;
}
