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

#include <chef/recipe.h>
#include <chef/platform.h>
#include <chef/list.h>
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

static int __parse_recipe(const char* yaml, struct recipe** out)
{
    return recipe_parse((void*)yaml, strlen(yaml), out);
}

// Helper to get nth item from a list
static struct list_item* __list_nth(struct list* list, int n)
{
    struct list_item* item;
    int i = 0;
    list_foreach(list, item) {
        if (i == n) return item;
        i++;
    }
    return NULL;
}

/**
 * Test: minimal recipe with only required project fields and one pack
 */
int test_recipe_minimal(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: test-project\n"
        "author: Test Author\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");
    TEST_ASSERT(recipe != NULL, "recipe should not be NULL");

    TEST_ASSERT(strcmp(recipe->project.name, "test-project") == 0,
        "project name should be 'test-project'");
    TEST_ASSERT(strcmp(recipe->project.author, "Test Author") == 0,
        "project author should be 'Test Author'");
    TEST_ASSERT(strcmp(recipe->project.email, "test@test.com") == 0,
        "project email should be 'test@test.com'");
    TEST_ASSERT(strcmp(recipe->project.version, "1.0.0") == 0,
        "project version should be '1.0.0'");

    TEST_ASSERT(recipe->packs.count == 1, "should have 1 pack");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: all project fields populated
 */
int test_recipe_project_fields(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: full-project\n"
        "author: Full Author\n"
        "email: full@test.com\n"
        "version: 2.3.4\n"
        "license: MIT\n"
        "eula: https://eula.example.com\n"
        "homepage: https://example.com\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    TEST_ASSERT(strcmp(recipe->project.name, "full-project") == 0,
        "project name should be 'full-project'");
    TEST_ASSERT(strcmp(recipe->project.version, "2.3.4") == 0,
        "project version should be '2.3.4'");
    TEST_ASSERT(strcmp(recipe->project.license, "MIT") == 0,
        "project license should be 'MIT'");
    TEST_ASSERT(strcmp(recipe->project.eula, "https://eula.example.com") == 0,
        "project eula should match");
    TEST_ASSERT(strcmp(recipe->project.url, "https://example.com") == 0,
        "project url should match");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: platforms list parsing
 */
int test_recipe_platforms(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: platform-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "platforms:\n"
        "- name: linux\n"
        "  base: ubuntu-22.04\n"
        "  toolchain: my-toolchain\n"
        "  architectures:\n"
        "  - x86_64\n"
        "  - aarch64\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    TEST_ASSERT(recipe->platforms.count == 1, "should have 1 platform");

    struct recipe_platform* plat = (struct recipe_platform*)recipe->platforms.head;
    TEST_ASSERT(plat != NULL, "platform should not be NULL");
    TEST_ASSERT(strcmp(plat->name, "linux") == 0, "platform name should be 'linux'");
    TEST_ASSERT(strcmp(plat->base, "ubuntu-22.04") == 0, "platform base should be 'ubuntu-22.04'");
    TEST_ASSERT(strcmp(plat->toolchain, "my-toolchain") == 0, "platform toolchain should match");
    TEST_ASSERT(plat->archs.count == 2, "platform should have 2 archs");

    struct list_item_string* arch0 = (struct list_item_string*)plat->archs.head;
    TEST_ASSERT(strcmp(arch0->value, "x86_64") == 0, "first arch should be x86_64");
    struct list_item_string* arch1 = (struct list_item_string*)arch0->list_header.next;
    TEST_ASSERT(strcmp(arch1->value, "aarch64") == 0, "second arch should be aarch64");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: ingredient lists (build and runtime)
 */
int test_recipe_ingredients(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: ingredient-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "environment:\n"
        "  build:\n"
        "    ingredients:\n"
        "    - name: org/build-dep\n"
        "      channel: stable\n"
        "  runtime:\n"
        "    ingredients:\n"
        "    - name: org/runtime-dep\n"
        "      channel: latest\n"
        "      include-filters:\n"
        "      - lib/**\n"
        "      - include/**\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    // build ingredients
    TEST_ASSERT(recipe->environment.build.ingredients.count == 1,
        "should have 1 build ingredient");
    struct recipe_ingredient* build_ing =
        (struct recipe_ingredient*)recipe->environment.build.ingredients.head;
    TEST_ASSERT(strcmp(build_ing->name, "org/build-dep") == 0,
        "build ingredient name should match");
    TEST_ASSERT(strcmp(build_ing->channel, "stable") == 0,
        "build ingredient channel should be 'stable'");
    TEST_ASSERT(build_ing->type == RECIPE_INGREDIENT_TYPE_BUILD,
        "build ingredient type should be BUILD");

    // runtime ingredients
    TEST_ASSERT(recipe->environment.runtime.ingredients.count == 1,
        "should have 1 runtime ingredient");
    struct recipe_ingredient* rt_ing =
        (struct recipe_ingredient*)recipe->environment.runtime.ingredients.head;
    TEST_ASSERT(strcmp(rt_ing->name, "org/runtime-dep") == 0,
        "runtime ingredient name should match");
    TEST_ASSERT(strcmp(rt_ing->channel, "latest") == 0,
        "runtime ingredient channel should be 'latest'");
    TEST_ASSERT(rt_ing->type == RECIPE_INGREDIENT_TYPE_RUNTIME,
        "runtime ingredient type should be RUNTIME");
    TEST_ASSERT(rt_ing->filters.count == 2,
        "runtime ingredient should have 2 filters");

    struct list_item_string* f0 = (struct list_item_string*)rt_ing->filters.head;
    TEST_ASSERT(strcmp(f0->value, "lib/**") == 0, "first filter should be 'lib/**'");
    struct list_item_string* f1 = (struct list_item_string*)f0->list_header.next;
    TEST_ASSERT(strcmp(f1->value, "include/**") == 0, "second filter should be 'include/**'");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: recipe parts with steps, dependencies, and env vars
 */
int test_recipe_parts_and_steps(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: parts-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "recipes:\n"
        "- name: my-part\n"
        "  source:\n"
        "    type: path\n"
        "    path: /src\n"
        "  toolchain: gcc\n"
        "  steps:\n"
        "  - name: configure\n"
        "    type: generate\n"
        "    system: cmake\n"
        "    arguments: [-DCMAKE_BUILD_TYPE=Release]\n"
        "    env:\n"
        "      CC: gcc\n"
        "      CFLAGS: -O2\n"
        "  - name: build\n"
        "    type: build\n"
        "    system: cmake\n"
        "    depends: [configure]\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    TEST_ASSERT(recipe->parts.count == 1, "should have 1 part");

    struct recipe_part* part = (struct recipe_part*)recipe->parts.head;
    TEST_ASSERT(strcmp(part->name, "my-part") == 0, "part name should be 'my-part'");
    TEST_ASSERT(strcmp(part->toolchain, "gcc") == 0, "part toolchain should be 'gcc'");

    // source should be path type from 'path: /src'
    TEST_ASSERT(part->source.type == RECIPE_PART_SOURCE_TYPE_PATH,
        "source type should be PATH");
    TEST_ASSERT(strcmp(part->source.path.path, "/src") == 0,
        "source path should be '/src'");

    TEST_ASSERT(part->steps.count == 2, "part should have 2 steps");

    // step 1: configure
    struct recipe_step* step0 = (struct recipe_step*)part->steps.head;
    TEST_ASSERT(strcmp(step0->name, "configure") == 0,
        "first step name should be 'configure'");
    TEST_ASSERT(step0->type == RECIPE_STEP_TYPE_GENERATE,
        "first step type should be GENERATE");
    TEST_ASSERT(strcmp(step0->system, "cmake") == 0,
        "first step system should be 'cmake'");
    TEST_ASSERT(step0->arguments.count == 1,
        "configure step should have 1 argument");

    struct list_item_string* arg0 =
        (struct list_item_string*)step0->arguments.head;
    TEST_ASSERT(strcmp(arg0->value, "-DCMAKE_BUILD_TYPE=Release") == 0,
        "argument should match");

    // env vars
    TEST_ASSERT(step0->env_keypairs.count == 2,
        "configure step should have 2 env vars");
    struct chef_keypair_item* env0 =
        (struct chef_keypair_item*)step0->env_keypairs.head;
    TEST_ASSERT(strcmp(env0->key, "CC") == 0, "first env key should be 'CC'");
    TEST_ASSERT(strcmp(env0->value, "gcc") == 0, "first env value should be 'gcc'");

    struct chef_keypair_item* env1 =
        (struct chef_keypair_item*)env0->list_header.next;
    TEST_ASSERT(strcmp(env1->key, "CFLAGS") == 0, "second env key should be 'CFLAGS'");
    TEST_ASSERT(strcmp(env1->value, "-O2") == 0, "second env value should be '-O2'");

    // step 2: build with dependency
    struct recipe_step* step1 = (struct recipe_step*)step0->list_header.next;
    TEST_ASSERT(strcmp(step1->name, "build") == 0,
        "second step name should be 'build'");
    TEST_ASSERT(step1->type == RECIPE_STEP_TYPE_BUILD,
        "second step type should be BUILD");
    TEST_ASSERT(step1->depends.count == 1,
        "build step should have 1 dependency");

    struct list_item_string* dep0 =
        (struct list_item_string*)step1->depends.head;
    TEST_ASSERT(strcmp(dep0->value, "configure") == 0,
        "dependency should be 'configure'");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: packs with filters and ingredient options
 */
int test_recipe_packs(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: pack-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "packs:\n"
        "- name: my-lib\n"
        "  summary: A library\n"
        "  description: A test library pack\n"
        "  type: ingredient\n"
        "  filters:\n"
        "  - lib/**\n"
        "  - include/**\n"
        "  ingredient-options:\n"
        "    bin-paths: [bin]\n"
        "    include-paths: [include]\n"
        "    lib-paths: [lib]\n"
        "    compiler-args: [-fPIC]\n"
        "    linker-args: [-lm]\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    TEST_ASSERT(recipe->packs.count == 1, "should have 1 pack");

    struct recipe_pack* pack = (struct recipe_pack*)recipe->packs.head;
    TEST_ASSERT(strcmp(pack->name, "my-lib") == 0, "pack name should be 'my-lib'");
    TEST_ASSERT(strcmp(pack->summary, "A library") == 0, "pack summary should match");
    TEST_ASSERT(strcmp(pack->description, "A test library pack") == 0,
        "pack description should match");
    TEST_ASSERT(pack->type == CHEF_PACKAGE_TYPE_INGREDIENT,
        "pack type should be INGREDIENT");

    // filters
    TEST_ASSERT(pack->filters.count == 2, "pack should have 2 filters");
    struct list_item_string* filt0 = (struct list_item_string*)pack->filters.head;
    TEST_ASSERT(strcmp(filt0->value, "lib/**") == 0, "first filter should be 'lib/**'");
    struct list_item_string* filt1 = (struct list_item_string*)filt0->list_header.next;
    TEST_ASSERT(strcmp(filt1->value, "include/**") == 0,
        "second filter should be 'include/**'");

    // ingredient options
    TEST_ASSERT(pack->options.bin_dirs.count == 1, "should have 1 bin_dir");
    TEST_ASSERT(pack->options.inc_dirs.count == 1, "should have 1 inc_dir");
    TEST_ASSERT(pack->options.lib_dirs.count == 1, "should have 1 lib_dir");
    TEST_ASSERT(pack->options.compiler_flags.count == 1, "should have 1 compiler_flag");
    TEST_ASSERT(pack->options.linker_flags.count == 1, "should have 1 linker_flag");

    struct list_item_string* bin =
        (struct list_item_string*)pack->options.bin_dirs.head;
    TEST_ASSERT(strcmp(bin->value, "bin") == 0, "bin_dir should be 'bin'");

    struct list_item_string* lflags =
        (struct list_item_string*)pack->options.linker_flags.head;
    TEST_ASSERT(strcmp(lflags->value, "-lm") == 0, "linker flag should be '-lm'");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: pack commands
 */
int test_recipe_pack_commands(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: command-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "packs:\n"
        "- name: my-app\n"
        "  summary: An application\n"
        "  type: application\n"
        "  commands:\n"
        "  - name: run\n"
        "    path: /usr/bin/app\n"
        "    type: executable\n"
        "    description: Run the app\n"
        "    arguments: [--verbose, --config, /etc/app.conf]\n"
        "  - name: server\n"
        "    path: /usr/bin/server\n"
        "    type: daemon\n"
        "    description: Start the server\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    struct recipe_pack* pack = (struct recipe_pack*)recipe->packs.head;
    TEST_ASSERT(pack->type == CHEF_PACKAGE_TYPE_APPLICATION,
        "pack type should be APPLICATION");
    TEST_ASSERT(pack->commands.count == 2, "pack should have 2 commands");

    // first command
    struct recipe_pack_command* cmd0 =
        (struct recipe_pack_command*)pack->commands.head;
    TEST_ASSERT(strcmp(cmd0->name, "run") == 0, "first command name should be 'run'");
    TEST_ASSERT(strcmp(cmd0->path, "/usr/bin/app") == 0,
        "first command path should match");
    TEST_ASSERT(cmd0->type == CHEF_COMMAND_TYPE_EXECUTABLE,
        "first command type should be EXECUTABLE");
    TEST_ASSERT(strcmp(cmd0->description, "Run the app") == 0,
        "first command description should match");
    TEST_ASSERT(cmd0->arguments.count == 3,
        "first command should have 3 arguments");

    // second command
    struct recipe_pack_command* cmd1 =
        (struct recipe_pack_command*)cmd0->list_header.next;
    TEST_ASSERT(strcmp(cmd1->name, "server") == 0,
        "second command name should be 'server'");
    TEST_ASSERT(cmd1->type == CHEF_COMMAND_TYPE_DAEMON,
        "second command type should be DAEMON");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: pack capabilities with network-client allow list
 */
int test_recipe_pack_capabilities(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: capability-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "packs:\n"
        "- name: my-app\n"
        "  summary: An application\n"
        "  type: application\n"
        "  capabilities:\n"
        "  - name: network-client\n"
        "    config:\n"
        "      allow:\n"
        "      - tcp: 80\n"
        "      - udp: 53\n"
        "      - tcp: 443\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    struct recipe_pack* pack = (struct recipe_pack*)recipe->packs.head;
    TEST_ASSERT(pack->capabilities.count == 1, "pack should have 1 capability");

    struct recipe_pack_capability* cap =
        (struct recipe_pack_capability*)pack->capabilities.head;
    TEST_ASSERT(strcmp(cap->name, "network-client") == 0,
        "capability name should be 'network-client'");
    TEST_ASSERT(cap->config.network_client.allow.count == 3,
        "network-client should have 3 allow entries");

    // entries are stored as "proto:port" strings
    struct list_item_string* a0 =
        (struct list_item_string*)cap->config.network_client.allow.head;
    TEST_ASSERT(strcmp(a0->value, "tcp:80") == 0,
        "first allow entry should be 'tcp:80'");
    struct list_item_string* a1 =
        (struct list_item_string*)a0->list_header.next;
    TEST_ASSERT(strcmp(a1->value, "udp:53") == 0,
        "second allow entry should be 'udp:53'");
    struct list_item_string* a2 =
        (struct list_item_string*)a1->list_header.next;
    TEST_ASSERT(strcmp(a2->value, "tcp:443") == 0,
        "third allow entry should be 'tcp:443'");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: environment section (host, build, runtime, hooks)
 */
int test_recipe_environment(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: env-test\n"
        "author: Test\n"
        "email: test@test.com\n"
        "version: 1.0.0\n"
        "\n"
        "environment:\n"
        "  host:\n"
        "    base: no\n"
        "    packages:\n"
        "    - build-essential\n"
        "    - cmake\n"
        "  build:\n"
        "    confinement: false\n"
        "  hooks:\n"
        "    setup: setup.sh\n"
        "\n"
        "packs:\n"
        "- name: test-pack\n"
        "  summary: A test pack\n"
        "  type: ingredient\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed");

    // host
    TEST_ASSERT(recipe->environment.host.base == 0,
        "host base should be disabled (0)");
    TEST_ASSERT(recipe->environment.host.packages.count == 2,
        "host should have 2 packages");
    struct list_item_string* pkg0 =
        (struct list_item_string*)recipe->environment.host.packages.head;
    TEST_ASSERT(strcmp(pkg0->value, "build-essential") == 0,
        "first package should be 'build-essential'");
    struct list_item_string* pkg1 =
        (struct list_item_string*)pkg0->list_header.next;
    TEST_ASSERT(strcmp(pkg1->value, "cmake") == 0,
        "second package should be 'cmake'");

    // build
    TEST_ASSERT(recipe->environment.build.confinement == 0,
        "build confinement should be disabled (0)");

    // hooks
    TEST_ASSERT(recipe->environment.hooks.setup != NULL,
        "hooks setup should not be NULL");
    TEST_ASSERT(strcmp(recipe->environment.hooks.setup, "setup.sh") == 0,
        "hooks setup should be 'setup.sh'");

    recipe_destroy(recipe);
    return 0;
}

/**
 * Test: full recipe with multiple sections populated
 */
int test_recipe_full(void)
{
    struct recipe* recipe = NULL;
    const char* yaml =
        "name: full-recipe\n"
        "author: Full Author\n"
        "email: full@test.com\n"
        "version: 3.2.1\n"
        "license: Apache-2.0\n"
        "homepage: https://full.example.com\n"
        "\n"
        "platforms:\n"
        "- name: linux\n"
        "  architectures:\n"
        "  - x86_64\n"
        "\n"
        "environment:\n"
        "  host:\n"
        "    packages:\n"
        "    - gcc\n"
        "  build:\n"
        "    ingredients:\n"
        "    - name: org/zlib\n"
        "      channel: stable\n"
        "  runtime:\n"
        "    ingredients:\n"
        "    - name: org/openssl\n"
        "      channel: stable\n"
        "\n"
        "recipes:\n"
        "- name: main\n"
        "  source:\n"
        "    type: path\n"
        "    path: /\n"
        "  steps:\n"
        "  - name: config\n"
        "    type: generate\n"
        "    system: cmake\n"
        "  - name: build\n"
        "    type: build\n"
        "    system: cmake\n"
        "    depends: [config]\n"
        "\n"
        "packs:\n"
        "- name: my-pkg\n"
        "  summary: A full package\n"
        "  description: Full test package\n"
        "  type: application\n"
        "  commands:\n"
        "  - name: app\n"
        "    path: /bin/app\n"
        "    type: executable\n"
        "    description: main app\n"
        "- name: my-lib\n"
        "  summary: A library\n"
        "  type: ingredient\n"
        "  filters:\n"
        "  - lib/**\n";

    int status = __parse_recipe(yaml, &recipe);
    TEST_ASSERT(status == 0, "recipe_parse should succeed for full recipe");
    TEST_ASSERT(recipe != NULL, "recipe should not be NULL");

    // project
    TEST_ASSERT(strcmp(recipe->project.name, "full-recipe") == 0,
        "project name should match");
    TEST_ASSERT(strcmp(recipe->project.version, "3.2.1") == 0,
        "project version should match");
    TEST_ASSERT(strcmp(recipe->project.license, "Apache-2.0") == 0,
        "project license should match");

    // platforms
    TEST_ASSERT(recipe->platforms.count == 1, "should have 1 platform");

    // environment (postprocess adds build-essential and cmake implicitly)
    TEST_ASSERT(recipe->environment.host.packages.count == 3,
        "should have 3 host packages (gcc + build-essential + cmake)");
    TEST_ASSERT(recipe->environment.build.ingredients.count == 1,
        "should have 1 build ingredient");
    TEST_ASSERT(recipe->environment.runtime.ingredients.count == 1,
        "should have 1 runtime ingredient");

    // parts
    TEST_ASSERT(recipe->parts.count == 1, "should have 1 part");
    struct recipe_part* part = (struct recipe_part*)recipe->parts.head;
    TEST_ASSERT(part->steps.count == 2, "part should have 2 steps");

    // packs
    TEST_ASSERT(recipe->packs.count == 2, "should have 2 packs");
    struct recipe_pack* pack0 = (struct recipe_pack*)recipe->packs.head;
    TEST_ASSERT(pack0->type == CHEF_PACKAGE_TYPE_APPLICATION,
        "first pack type should be APPLICATION");
    TEST_ASSERT(pack0->commands.count == 1,
        "first pack should have 1 command");

    struct recipe_pack* pack1 = (struct recipe_pack*)pack0->list_header.next;
    TEST_ASSERT(pack1->type == CHEF_PACKAGE_TYPE_INGREDIENT,
        "second pack type should be INGREDIENT");
    TEST_ASSERT(pack1->filters.count == 1,
        "second pack should have 1 filter");

    recipe_destroy(recipe);
    return 0;
}
