/* Exercise parsed recipe steps through the real oven without containers/root. */
#include <chef/recipe.h>
#include <chef/platform.h>
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Read a test override while retaining the production default.
 */
static const char* __test_value_or_default(const char* name, const char* fallback)
{
    const char* value = getenv(name);

    return value != NULL ? value : fallback;
}

int main(int argc, char** argv, char** envp)
{
    struct recipe* recipe = NULL;
    struct list_item* item;
    FILE* file;
    char* yaml;
    long size;
    int status;
    if (argc != 7) {
        return 1;
    }
    file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0) {
        return 1;
    }
    rewind(file);
    yaml = malloc((size_t)size + 1);
    if (!yaml || fread(yaml, 1, (size_t)size, file) != (size_t)size) {
        return 1;
    }
    fclose(file);
    status = recipe_parse(yaml, (size_t)size, &recipe);
    free(yaml);
    if (status) {
        return 1;
    }
    struct oven_initialize_options init = {
        .envp = (const char* const*)envp,
        .target_platform = __test_value_or_default("CHEF_TEST_PLATFORM", CHEF_PLATFORM_STR),
        .target_architecture = __test_value_or_default("CHEF_TEST_ARCH", CHEF_ARCHITECTURE_STR),
        .paths = {
            .project_root = argv[2],
            .source_root = argv[3],
            .build_root = argv[4],
            .install_root = argv[5],
            .toolchains_root = argv[6],
            .build_ingredients_root = argv[6]
        }
    };
    status = oven_initialize(&init);
    if (status) {
        recipe_destroy(recipe);
        return 1;
    }
    struct recipe_part* part = (struct recipe_part*)recipe->parts.head;
    status = oven_recipe_start(&(struct oven_recipe_options){ .name = part->name });
    if (status) {
        goto cleanup;
    }
    list_foreach(&part->steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        // Clean mode exercises only build steps, matching oven_clean's API.
        if (getenv("CHEF_TEST_CLEAN") != NULL) {
            // Generate steps have no build output for a clean backend to remove.
            if (step->type != RECIPE_STEP_TYPE_BUILD) {
                continue;
            }

            struct oven_clean_options clean = {
                .name = step->name,
                .system = step->system,
                .system_options = &step->options,
                .arguments = &step->arguments,
                .environment = &step->env_keypairs,
                .source_dir = step->configure.source_dir != NULL
                    ? step->configure.source_dir
                    : step->source_dir
            };
            status = oven_clean(&clean);
        } else if (step->type == RECIPE_STEP_TYPE_GENERATE) {
            struct oven_generate_options generate = {
                .name = step->name,
                .system = step->system,
                .system_options = &step->options,
                .source_dir = step->source_dir,
                .arguments = &step->arguments,
                .environment = &step->env_keypairs
            };
            status = oven_configure(&generate);
        } else {
            struct oven_build_options build = {
                .name = step->name,
                .system = step->system,
                .system_options = &step->options,
                .arguments = &step->arguments,
                .environment = &step->env_keypairs,
                .skip_generate = step->configure.disabled,
                .source_dir = step->configure.source_dir != NULL
                    ? step->configure.source_dir
                    : step->source_dir,
                .generate_arguments = &step->configure.arguments,
                .generate_environment = &step->configure.env_keypairs
            };
            status = oven_build(&build);
        }
        if (status) {
            break;
        }
    }
    oven_recipe_end();
cleanup:
    oven_cleanup();
    recipe_destroy(recipe);
    return status ? 1 : 0;
}
