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
#include <chef/utils_vafs.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/builder.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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
    free((void*)tmpDir);
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

static int __write_manifest_with_toolchain_feature(
    const char*               outputPath,
    struct VaFsFeatureHeader* toolchainFeature)
{
    struct VaFsGuid packageGuid = CHEF_PACKAGE_HEADER_GUID;
    struct VaFsGuid versionGuid = CHEF_PACKAGE_VERSION_GUID;
    struct VaFsBuilderConfiguration configuration;
    struct chef_vafs_feature_package_header packageHeader = { 0 };
    struct chef_vafs_feature_package_version packageVersion = { 0 };
    struct VaFsDirectoryBuilder* root = NULL;
    struct VaFs* vafs = NULL;
    int status;

    vafs_builder_config_initialize(&configuration);
    status = vafs_builder_new(outputPath, &configuration, &vafs, &root);
    if (status != 0) {
        return status;
    }

    memcpy(&packageHeader.header.Guid, &packageGuid, sizeof(packageGuid));
    packageHeader.header.Length = sizeof(packageHeader);
    packageHeader.version = CHEF_PACKAGE_VERSION;
    packageHeader.type = CHEF_PACKAGE_TYPE_TOOLCHAIN;
    status = vafs_builder_add_feature(vafs, &packageHeader.header);

    memcpy(&packageVersion.header.Guid, &versionGuid, sizeof(versionGuid));
    packageVersion.header.Length = sizeof(packageVersion);
    if (status == 0) {
        status = vafs_builder_add_feature(vafs, &packageVersion.header);
    }
    if (status == 0) {
        status = vafs_builder_add_feature(vafs, toolchainFeature);
    }

    vafs_directory_builder_close(root);
    if (vafs_builder_close(vafs) != 0 && status == 0) {
        status = -1;
    }
    return status;
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

int test_package_manifest_toolchain_roundtrip(void)
{
    static const char* valiCompilerArgs[] = {
        "--target=$[[ TOOLCHAIN_TARGET_TRIPLE ]]",
        "-Wl,-rpath,/opt/sdk/lib"
    };
    struct chef_package_manifest_toolchain_target targets[] = {
        { "vali", "$[[ CHEF_TARGET_ARCHITECTURE ]]-uml-vali", { valiCompilerArgs, 2 } },
        { "linux", NULL, { NULL, 0 } }
    };
    struct chef_package_manifest manifest = {
        .name = "vali/clang-cc",
        .platform = "linux",
        .architecture = "amd64",
        .type = CHEF_PACKAGE_TYPE_TOOLCHAIN,
        .summary = "LLVM toolchain",
        .version = { 18, 1, 0, 0, NULL, 0, NULL },
        .toolchain = {
            .root = "usr/local",
            .cc = "bin/clang",
            .cxx = "bin/clang++",
            .ar = "bin/llvm-ar",
            .ranlib = "bin/llvm-ranlib",
            .strip = "bin/llvm-strip",
            .llvm_config = "bin/llvm-config",
            .targets = targets,
            .targets_count = 2
        }
    };
    struct chef_package_manifest* loaded = NULL;
    char inputDir[PATH_MAX];
    char path[PATH_MAX];
    int status;

    TEST_ASSERT(__create_temp_paths(&inputDir[0], sizeof(inputDir), &path[0], sizeof(path)) == 0, "temp path should be created");
    TEST_ASSERT(__create_test_input_dir(&inputDir[0]) == 0, "input directory should be created");
    status = __write_manifest_file(&inputDir[0], &path[0], &manifest);
    TEST_ASSERT(status == 0, "manifest write should succeed");

    status = chef_package_manifest_load(&path[0], &loaded);
    __remove_test_input_dir(&inputDir[0]);
    remove(&path[0]);
    TEST_ASSERT(status == 0, "manifest load should succeed");
    TEST_ASSERT(strcmp(loaded->toolchain.root, "usr/local") == 0, "toolchain root should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.cc, "bin/clang") == 0, "toolchain cc should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.cxx, "bin/clang++") == 0, "toolchain cxx should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.llvm_config, "bin/llvm-config") == 0, "llvm-config should roundtrip");
    TEST_ASSERT(loaded->toolchain.targets_count == 2, "target count should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.targets[0].name, "vali") == 0, "first target name should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.targets[0].triple,
        "$[[ CHEF_TARGET_ARCHITECTURE ]]-uml-vali") == 0, "target triple should roundtrip");
    TEST_ASSERT(loaded->toolchain.targets[0].compiler_args.count == 2,
        "target compiler argument count should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.targets[0].compiler_args.values[0],
        "--target=$[[ TOOLCHAIN_TARGET_TRIPLE ]]") == 0, "target compiler argument should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.targets[0].compiler_args.values[1],
        "-Wl,-rpath,/opt/sdk/lib") == 0, "compiler argument commas should roundtrip");
    TEST_ASSERT(strcmp(loaded->toolchain.targets[1].name, "linux") == 0, "second target name should roundtrip");
    TEST_ASSERT(loaded->toolchain.targets[1].triple == NULL, "optional target triple should remain NULL");

    chef_package_manifest_free(loaded);
    return 0;
}

int test_package_manifest_rejects_malformed_toolchain_metadata(void)
{
    struct VaFsGuid toolchainGuid = CHEF_PACKAGE_TOOLCHAIN_OPTS_GUID;
    struct {
        struct chef_vafs_feature_toolchain_opts header;
        struct chef_vafs_toolchain_target target;
        uint32_t argument_length;
    } feature;
    struct chef_package_manifest* loaded = NULL;
    char inputDir[PATH_MAX];
    char path[PATH_MAX];
    int status;

    TEST_ASSERT(__create_temp_paths(&inputDir[0], sizeof(inputDir), &path[0], sizeof(path)) == 0,
        "temp path should be created");

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.header.header.Guid, &toolchainGuid, sizeof(toolchainGuid));
    feature.header.header.Length = sizeof(struct VaFsFeatureHeader);
    TEST_ASSERT(__write_manifest_with_toolchain_feature(path, &feature.header.header) == 0,
        "undersized toolchain feature should be written");
    status = chef_package_manifest_load(path, &loaded);
    remove(path);
    TEST_ASSERT(status != 0 && loaded == NULL, "undersized toolchain header should be rejected");

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.header.header.Guid, &toolchainGuid, sizeof(toolchainGuid));
    feature.header.header.Length = sizeof(feature.header);
    feature.header.root_length = 1;
    TEST_ASSERT(__write_manifest_with_toolchain_feature(path, &feature.header.header) == 0,
        "truncated string feature should be written");
    status = chef_package_manifest_load(path, &loaded);
    remove(path);
    TEST_ASSERT(status != 0 && loaded == NULL, "truncated toolchain string should be rejected");

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.header.header.Guid, &toolchainGuid, sizeof(toolchainGuid));
    feature.header.header.Length = sizeof(feature.header);
    feature.header.targets_count = 1;
    TEST_ASSERT(__write_manifest_with_toolchain_feature(path, &feature.header.header) == 0,
        "truncated target feature should be written");
    status = chef_package_manifest_load(path, &loaded);
    remove(path);
    TEST_ASSERT(status != 0 && loaded == NULL, "truncated toolchain target should be rejected");

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.header.header.Guid, &toolchainGuid, sizeof(toolchainGuid));
    feature.header.header.Length = sizeof(feature.header) + sizeof(feature.target);
    feature.header.targets_count = 1;
    feature.target.compiler_args_count = 1;
    TEST_ASSERT(__write_manifest_with_toolchain_feature(path, &feature.header.header) == 0,
        "truncated argument count feature should be written");
    status = chef_package_manifest_load(path, &loaded);
    remove(path);
    TEST_ASSERT(status != 0 && loaded == NULL, "impossible compiler argument count should be rejected");

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.header.header.Guid, &toolchainGuid, sizeof(toolchainGuid));
    feature.header.header.Length = sizeof(feature);
    feature.header.targets_count = 1;
    feature.target.compiler_args_count = 1;
    feature.argument_length = 1;
    TEST_ASSERT(__write_manifest_with_toolchain_feature(path, &feature.header.header) == 0,
        "truncated argument feature should be written");
    status = chef_package_manifest_load(path, &loaded);
    remove(path);
    TEST_ASSERT(status != 0 && loaded == NULL, "truncated compiler argument should be rejected");
    return 0;
}