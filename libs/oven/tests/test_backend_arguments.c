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

/**
 * Exercise literal backend arguments, preprocessing, and backend path helpers.
 */

#include "../backends/private.h"
#include <chef/environment.h>
#include <errno.h>
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static size_t g_outputBytes[2];

/**
 * @brief Count bytes delivered through a platform spawn output callback.
 */
static void __count_output(const char* text, enum platform_spawn_output_type type)
{
    size_t index = type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT ? 0 : 1;

    g_outputBytes[index] += strlen(text);
}

/**
 * @brief Resolve the one variable used by the preprocessing tests.
 */
static const char* __resolve_test(const char* key, void* context)
{
    // Resolve only the variable intentionally supplied by this test.
    if (strcmp(key, "VALUE") == 0) {
        return (const char*)context;
    }

    return NULL;
}

static const char* g_samples[] = {
    "",
    "a b",
    "C:\\Program Files\\SDK\\",
    "-DKEY=a b",
    "a\"b",
    "single'quote",
    "percent %s %n %%",
    "semi;colon",
    "tab\there",
    "plain",
    NULL
};

int main(int argc, char** argv, char** envp)
{
    char input[] = "  -DKEY=\"a b\"\t'other value' \"\" C:\\tools\\bin tail   ";
    char bad[] = "one \"unfinished";
    const char* simple[] = { "C:\\a b\\", "a\"b", "", NULL };
    const char* child[12] = { "--child" };
    const char* outputArgs[] = { "--output", NULL };
    const char* paths[] = { "one", "two", "three", NULL };
    char** parsed;
    char* command;
    char* expanded;
    char* large;
    char* flattened;
    char* prefix;
    char* expectedPrefix;
    size_t length;
    int count;

    /*
     * The executable doubles as the child process. These modes produce data
     * that exercises the argument round trip and output callback paths.
     */
    // The output mode emits enough data to exercise repeated pipe reads.
    if (argc > 1 && strcmp(argv[1], "--output") == 0) {
        // Repeat a fixed chunk so both streams exceed the pipe read buffer.
        for (int i = 0; i < 4096; i++) {
            fputs("0123456789abcdef", stdout);
            fputs("%s%n%%0123456789", stderr);
        }
        return 0;
    }

    // The child mode verifies that every literal argument survived the spawn.
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        CHECK(argc == 12);
        // Compare the received arguments with the original serialization samples.
        for (int i = 0; g_samples[i] != NULL; i++) {
            CHECK(strcmp(argv[i + 2], g_samples[i]) == 0);
        }
        return 0;
    }

    oven_cleanup();

    parsed = strargv(input, NULL, &count);
    CHECK(parsed != NULL && count == 5);
    CHECK(strcmp(parsed[0], "-DKEY=a b") == 0);
    CHECK(strcmp(parsed[1], "other value") == 0);
    CHECK(strcmp(parsed[2], "") == 0);
    CHECK(strcmp(parsed[3], "C:\\tools\\bin") == 0);
    CHECK(strcmp(parsed[4], "tail") == 0 && parsed[5] == NULL);
    strargv_free(parsed);

    CHECK(strargv(bad, NULL, NULL) == NULL && errno == EINVAL);
    parsed = strargv(NULL, "tool", &count);
    CHECK(parsed != NULL && count == 1 && strcmp(parsed[0], "tool") == 0);
    strargv_free(parsed);

    command = strargv_windows(g_samples);
    CHECK(command != NULL);
    parsed = strargv(command, NULL, &count);
    CHECK(parsed != NULL && count == 10);
    
    // Verify that the Windows serializer and parser are inverse operations.
    for (int i = 0; g_samples[i] != NULL; i++) {
        CHECK(strcmp(parsed[i], g_samples[i]) == 0);
    }
    strargv_free(parsed);
    free(command);

    command = strargv_windows(simple);
    CHECK(command != NULL && strcmp(command, "\"C:\\a b\\\\\" \"a\\\"b\" \"\"") == 0);
    free(command);

    // Pass the complete sample set to the child process after its mode flag.
    for (int i = 0; i < 10; i++) {
        child[i + 1] = g_samples[i];
    }

    CHECK(platform_spawn_argv(argv[0], child, (const char* const*)envp, NULL) == 0);
    CHECK(platform_spawn_argv(argv[0], outputArgs, (const char* const*)envp,
        &(struct platform_spawn_options) {
            .output_handler = __count_output
        }) == 0);
    CHECK(g_outputBytes[0] == 4096 * 16);
    CHECK(g_outputBytes[1] == 4096 * 16);

    large = malloc(12001);
    CHECK(large != NULL);
    memset(large, 'x', 12000);
    large[12000] = '\0';

    expanded = chef_preprocess_text("before$[[ VALUE ]]after$[[ VALUE ]]",
        __resolve_test, large);
    CHECK(expanded != NULL);
    CHECK(strlen(expanded) == 24011);
    CHECK(strncmp(expanded, "before", 6) == 0);
    free(expanded);

    expanded = chef_preprocess_text(large, NULL, NULL);
    CHECK(expanded != NULL && strcmp(expanded, large) == 0);
    free(expanded);
    free(large);

    if (getenv("PATH") != NULL) {
        expanded = chef_preprocess_text("$[ PATH ]", NULL, NULL);
        CHECK(expanded != NULL && strcmp(expanded, getenv("PATH")) == 0);
        free(expanded);
    }

    CHECK(chef_preprocess_text("$[[ UNKNOWN ]]", __resolve_test, "x") == NULL);
    CHECK(errno == ENOENT);
    CHECK(chef_preprocess_text("$[[ ]]", __resolve_test, "x") == NULL);
    CHECK(errno == EINVAL);
    CHECK(chef_preprocess_text("$[[ VALUE", __resolve_test, "x") == NULL);
    CHECK(errno == EINVAL);

    flattened = strflatten(paths, ";", &length);
    CHECK(flattened != NULL);
    CHECK(strcmp(flattened, "one;two;three") == 0);
    CHECK(length == 14);
    free(flattened);

    prefix = backend_install_prefix("/tmp/install", "/tmp/install-other");
    expectedPrefix = strpathcombine("/tmp/install", "tmp/install-other");
    CHECK(prefix != NULL && expectedPrefix != NULL);
    CHECK(strcmp(prefix, expectedPrefix) == 0);
    free(expectedPrefix);
    free(prefix);

    prefix = backend_install_prefix("/tmp/install/", "/tmp/install/usr");
    CHECK(prefix != NULL && strcmp(prefix, "/tmp/install/usr") == 0);
    free(prefix);

    CHECK(backend_install_prefix("/tmp/install", "/tmp/install/../outside") == NULL);
    CHECK(errno == EINVAL);
    CHECK(backend_install_prefix("/tmp/install", "..\\outside") == NULL);
    CHECK(errno == EINVAL);

    CHECK(configure_main(NULL, NULL));
    CHECK(cmake_main(NULL, NULL));
    CHECK(cmake_build_main(NULL, NULL));
    CHECK(meson_config_main(NULL, NULL));
    CHECK(meson_build_main(NULL, NULL));
    CHECK(meson_clean_main(NULL, NULL));
    CHECK(make_build_main(NULL, NULL));
    CHECK(make_clean_main(NULL, NULL));
    CHECK(ninja_build_main(NULL, NULL));
    CHECK(ninja_clean_main(NULL, NULL));
    CHECK(cmake_clean_main(NULL, NULL));

    puts("Backend arguments passed");
    return 0;
}
