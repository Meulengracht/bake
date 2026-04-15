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

#include <chef/package_image.h>
#include <chef/platform.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#include <vafs/vafs.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while (0)

static int __create_temp_paths(
    char*  inputDirOut,
    size_t inputDirSize,
    char*  packPathOut,
    size_t packPathSize)
{
    const char* tmpDir;
    int         pid;
    int         token;

    tmpDir = platform_tmpdir();
    if (tmpDir == NULL) {
        return -1;
    }

#if defined(_WIN32)
    pid = (int)_getpid();
#else
    pid = (int)getpid();
#endif
    token = rand();
    snprintf(inputDirOut, inputDirSize, "%s/chef-package-input-%d-%d", tmpDir, pid, token);
    snprintf(packPathOut, packPathSize, "%s/chef-package-image-%d-%d.pack", tmpDir, pid, token);
    return 0;
}

static int __create_test_input_dir(const char* dirPath)
{
    char  filePath[PATH_MAX];
    FILE* file;

    if (platform_mkdir(dirPath) != 0) {
        return -1;
    }

    snprintf(filePath, sizeof(filePath), "%s/payload.txt", dirPath);
    file = fopen(filePath, "wb");
    if (file == NULL) {
        platform_rmdir(dirPath);
        return -1;
    }
    fwrite("payload", 1, 7, file);
    fclose(file);
    return 0;
}

static void __remove_test_input_dir(const char* dirPath)
{
    char filePath[PATH_MAX];

    snprintf(filePath, sizeof(filePath), "%s/payload.txt", dirPath);
    platform_unlink(filePath);
    platform_rmdir(dirPath);
}

static int __write_manifest_file(
    const char*                        inputDir,
    const char*                        outputPath,
    const struct chef_package_manifest* manifest)
{
    struct chef_package_image_options options;

    options.input_dir = inputDir;
    options.output_path = outputPath;
    options.filters = NULL;
    options.manifest = manifest;
    return chef_package_image_create(&options);
}

int test_package_manifest_application_roundtrip(void)
{
    static const char* capabilityAllow[] = {"tcp:443", "udp:53"};
    static const char  packageIcon[] = {1, 2, 3, 4};
    static const char  commandIcon[] = {9, 8, 7};
    static const struct chef_package_manifest_command commands[] = {
        {
            CHEF_COMMAND_TYPE_EXECUTABLE,
            "serve",
            "Launch the service",
            "--foreground --port 8080",
            "/usr/bin/serve",
            { &commandIcon[0], sizeof(commandIcon) }
        }
    };
    static const struct chef_package_manifest_capability capabilities[] = {
        {
            "network-client",
            CHEF_PACKAGE_MANIFEST_CAPABILITY_ALLOW_LIST,
            { capabilityAllow, 2 }
        }
    };
    struct chef_package_manifest manifest = {
        .name = "app/demo",
        .platform = "linux",
        .architecture = "amd64",
        .type = CHEF_PACKAGE_TYPE_APPLICATION,
        .base = "core/base",
        .summary = "Demo package",
        .description = "Roundtrip manifest test",
        .license = "MIT",
        .eula = "none",
        .maintainer = "Chef",
        .maintainer_email = "chef@example.com",
        .homepage = "https://example.com/demo",
        .version = { 1, 2, 3, 0, "+beta", 0, NULL },
        .icon = { &packageIcon[0], sizeof(packageIcon) },
        .commands = (struct chef_package_manifest_command*)&commands[0],
        .commands_count = 1,
        .application = { "10.0.0.1", "1.1.1.1" },
        .capabilities = (struct chef_package_manifest_capability*)&capabilities[0],
        .capabilities_count = 1,
    };
    struct chef_package_manifest* loaded = NULL;
    char                          inputDir[PATH_MAX];
    char                          path[PATH_MAX];
    int                           status;

    TEST_ASSERT(__create_temp_paths(&inputDir[0], sizeof(inputDir), &path[0], sizeof(path)) == 0, "temp path should be created");
    TEST_ASSERT(__create_test_input_dir(&inputDir[0]) == 0, "input directory should be created");
    status = __write_manifest_file(&inputDir[0], &path[0], &manifest);
    TEST_ASSERT(status == 0, "manifest write should succeed");

    status = chef_package_manifest_load(&path[0], &loaded);
    __remove_test_input_dir(&inputDir[0]);
    remove(&path[0]);
    TEST_ASSERT(status == 0, "manifest load should succeed");
    TEST_ASSERT(loaded != NULL, "loaded manifest should not be NULL");

    TEST_ASSERT(strcmp(loaded->name, manifest.name) == 0, "package name should roundtrip");
    TEST_ASSERT(strcmp(loaded->platform, manifest.platform) == 0, "platform should roundtrip");
    TEST_ASSERT(strcmp(loaded->architecture, manifest.architecture) == 0, "architecture should roundtrip");
    TEST_ASSERT(loaded->version.major == 1, "major version should match");
    TEST_ASSERT(loaded->version.minor == 2, "minor version should match");
    TEST_ASSERT(loaded->version.patch == 3, "patch version should match");
    TEST_ASSERT(strcmp(loaded->version.tag, "+beta") == 0, "version tag should roundtrip");
    TEST_ASSERT(loaded->icon.size == sizeof(packageIcon), "package icon size should match");
    TEST_ASSERT(memcmp(loaded->icon.data, packageIcon, sizeof(packageIcon)) == 0, "package icon bytes should match");
    TEST_ASSERT(loaded->commands_count == 1, "command count should match");
    TEST_ASSERT(strcmp(loaded->commands[0].name, "serve") == 0, "command name should match");
    TEST_ASSERT(strcmp(loaded->commands[0].arguments, "--foreground --port 8080") == 0, "command arguments should match");
    TEST_ASSERT(loaded->commands[0].icon.size == sizeof(commandIcon), "command icon size should match");
    TEST_ASSERT(strcmp(loaded->application.network_gateway, "10.0.0.1") == 0, "network gateway should roundtrip");
    TEST_ASSERT(strcmp(loaded->application.network_dns, "1.1.1.1") == 0, "network dns should roundtrip");
    TEST_ASSERT(loaded->capabilities_count == 1, "capability count should match");
    TEST_ASSERT(strcmp(loaded->capabilities[0].name, "network-client") == 0, "capability name should match");
    TEST_ASSERT(loaded->capabilities[0].allow_list.count == 2, "allow list should roundtrip");
    TEST_ASSERT(strcmp(loaded->capabilities[0].allow_list.values[0], "tcp:443") == 0, "first allow entry should match");
    TEST_ASSERT(strcmp(loaded->capabilities[0].allow_list.values[1], "udp:53") == 0, "second allow entry should match");

    chef_package_manifest_free(loaded);
    return 0;
}

int test_package_manifest_ingredient_roundtrip(void)
{
    static const char* binDirs[] = {"bin", "sbin"};
    static const char* incDirs[] = {"include/demo"};
    static const char* libDirs[] = {"lib", "lib64"};
    static const char* compilerFlags[] = {"-Iinclude/demo", "-DDEMO=1"};
    static const char* linkerFlags[] = {"-ldemo"};
    struct chef_package_manifest manifest = {
        .name = "ingredient/demo",
        .platform = "linux",
        .architecture = "amd64",
        .type = CHEF_PACKAGE_TYPE_INGREDIENT,
        .base = "core/base",
        .summary = "Ingredient package",
        .description = "Ingredient roundtrip",
        .version = { 2, 4, 6, 0, NULL, 0, NULL },
        .ingredient = {
            { binDirs, 2 },
            { incDirs, 1 },
            { libDirs, 2 },
            { compilerFlags, 2 },
            { linkerFlags, 1 },
        },
    };
    struct chef_package_manifest* loaded = NULL;
    char                          inputDir[PATH_MAX];
    char                          path[PATH_MAX];
    int                           status;

    TEST_ASSERT(__create_temp_paths(&inputDir[0], sizeof(inputDir), &path[0], sizeof(path)) == 0, "temp path should be created");
    TEST_ASSERT(__create_test_input_dir(&inputDir[0]) == 0, "input directory should be created");
    status = __write_manifest_file(&inputDir[0], &path[0], &manifest);
    TEST_ASSERT(status == 0, "manifest write should succeed");

    status = chef_package_manifest_load(&path[0], &loaded);
    __remove_test_input_dir(&inputDir[0]);
    remove(&path[0]);
    TEST_ASSERT(status == 0, "manifest load should succeed");
    TEST_ASSERT(loaded->ingredient.bin_dirs.count == 2, "bin dirs should roundtrip");
    TEST_ASSERT(strcmp(loaded->ingredient.bin_dirs.values[0], "bin") == 0, "first bin dir should match");
    TEST_ASSERT(strcmp(loaded->ingredient.bin_dirs.values[1], "sbin") == 0, "second bin dir should match");
    TEST_ASSERT(loaded->ingredient.lib_dirs.count == 2, "lib dirs should roundtrip");
    TEST_ASSERT(strcmp(loaded->ingredient.compiler_flags.values[1], "-DDEMO=1") == 0, "compiler flag should match");
    TEST_ASSERT(strcmp(loaded->ingredient.linker_flags.values[0], "-ldemo") == 0, "linker flag should match");

    chef_package_manifest_free(loaded);
    return 0;
}