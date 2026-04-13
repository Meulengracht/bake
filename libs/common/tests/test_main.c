/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <stdio.h>
#include <stdlib.h>

// recipe parser tests
extern int test_recipe_minimal(void);
extern int test_recipe_project_fields(void);
extern int test_recipe_platforms(void);
extern int test_recipe_ingredients(void);
extern int test_recipe_parts_and_steps(void);
extern int test_recipe_packs(void);
extern int test_recipe_pack_commands(void);
extern int test_recipe_pack_capabilities(void);
extern int test_recipe_environment(void);
extern int test_recipe_full(void);

// image parser tests
extern int test_image_mbr_minimal(void);
extern int test_image_gpt_minimal(void);
extern int test_image_partition_sources(void);
extern int test_image_partition_attributes(void);
extern int test_image_fat_options(void);
extern int test_image_multiple_partitions(void);
extern int test_image_full(void);

// package manifest tests
extern int test_package_manifest_application_roundtrip(void);
extern int test_package_manifest_ingredient_roundtrip(void);

typedef struct {
    const char* name;
    int (*func)(void);
} test_case_t;

static const test_case_t tests[] = {
    // recipe tests
    {"Recipe: minimal",              test_recipe_minimal},
    {"Recipe: project fields",       test_recipe_project_fields},
    {"Recipe: platforms",            test_recipe_platforms},
    {"Recipe: ingredients",          test_recipe_ingredients},
    {"Recipe: parts and steps",      test_recipe_parts_and_steps},
    {"Recipe: packs",                test_recipe_packs},
    {"Recipe: pack commands",        test_recipe_pack_commands},
    {"Recipe: pack capabilities",    test_recipe_pack_capabilities},
    {"Recipe: environment",          test_recipe_environment},
    {"Recipe: full",                 test_recipe_full},

    // image tests
    {"Image: MBR minimal",          test_image_mbr_minimal},
    {"Image: GPT minimal",          test_image_gpt_minimal},
    {"Image: partition sources",     test_image_partition_sources},
    {"Image: partition attributes",  test_image_partition_attributes},
    {"Image: FAT options",           test_image_fat_options},
    {"Image: multiple partitions",   test_image_multiple_partitions},
    {"Image: full",                  test_image_full},

    // package manifest tests
    {"Package manifest: application roundtrip", test_package_manifest_application_roundtrip},
    {"Package manifest: ingredient roundtrip", test_package_manifest_ingredient_roundtrip},
};

static const size_t num_tests = sizeof(tests) / sizeof(tests[0]);

int main(int argc, char** argv)
{
    int passed = 0;
    int failed = 0;

    (void)argc;
    (void)argv;

    printf("Running common parser tests...\n\n");

    for (size_t i = 0; i < num_tests; i++) {
        printf("Running: %s\n", tests[i].name);
        int result = tests[i].func();
        if (result == 0) {
            printf("  PASSED\n\n");
            passed++;
        } else {
            printf("  FAILED\n\n");
            failed++;
        }
    }

    printf("=================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("=================================\n");

    return failed > 0 ? 1 : 0;
}
