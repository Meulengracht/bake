/**
 * Copyright 2022, Philip Meulengracht
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

#include <backend.h>
#include <errno.h>
#include <liboven.h>
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVEN_ROOT ".oven"
#define OVEN_BUILD_ROOT OVEN_ROOT "/build"
#define OVEN_INSTALL_ROOT OVEN_ROOT "/install"

struct oven_recipe_context {
    const char* name;
    const char* relative_path;
    const char* toolchain;

    const char* build_root;
    const char* install_root;
    const char* checkpoint_path;
};

struct oven_variables {
    const char* cwd;
    const char* fridge_prep_directory;
};

struct oven_context {
    const char**               process_environment;
    const char*                build_root;
    const char*                install_root;
    struct oven_variables      variables;
    struct oven_recipe_context recipe;
};

struct generate_backend {
    const char* name;
    int       (*generate)(struct oven_backend_data* data);
};

struct build_backend {
    const char* name;
    int       (*build)(struct oven_backend_data* data);
};

static struct generate_backend g_genbackends[] = {
    { "configure", configure_main },
    { "cmake",     cmake_main     }
};

static struct build_backend g_buildbackends[] = {
    { "make", make_main }
};

static struct oven_context g_ovenContext = { 0 };

const char* __get_install_path(void)
{
    return g_ovenContext.install_root;
}

static int __get_cwd(char** bufferOut)
{
    char*  cwd;
    int    status;

    cwd = malloc(4096);
    if (cwd == NULL) {
        return -1;
    }

    status = platform_getcwd(cwd, 4096);
    if (status) {
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
}

static const char* __combine(const char* a, const char* b)
{
    char* combined;
    int   status;

    combined = malloc(strlen(a) + strlen(b) + 2);
    if (combined == NULL) {
        return NULL;
    }

    status = sprintf(combined, "%s/%s", a, b);
    if (status < 0) {
        free(combined);
        return NULL;
    }

    return combined;
}

static int __create_path(const char* path)
{
    if (platform_mkdir(path)) {
        if (errno != EEXIST) {
            fprintf(stderr, "oven: failed to create %s: %s\n", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int oven_initialize(char** envp, const char* recipeScope, const char* fridgePrepDirectory)
{
    int         status;
    char*       cwd;
    const char* root;
    const char* buildRoot;
    const char* installRoot;
    char        tmp[128];

    // get the current working directory
    status = __get_cwd(&cwd);
    if (status) {
        return -1;
    }

    // make sure to add the last path seperator if not already
    // present in cwd
    if (cwd[strlen(cwd) - 1] != '/') {
        strcat(cwd, "/");
    }

    // get basename of recipe
    strbasename(recipeScope, tmp, sizeof(tmp));

    // initialize oven paths
    root        = __combine(cwd, OVEN_ROOT);
    buildRoot   = __combine(cwd, OVEN_BUILD_ROOT);
    installRoot = __combine(cwd, OVEN_INSTALL_ROOT);

    if (root == NULL || buildRoot == NULL || installRoot == NULL) {
        free((void*)root);
        free((void*)buildRoot);
        free((void*)installRoot);
        return -1;
    }

    // update oven variables
    g_ovenContext.variables.cwd                   = cwd;
    g_ovenContext.variables.fridge_prep_directory = fridgePrepDirectory;

    // update oven context
    g_ovenContext.process_environment   = (const char**)envp;
    g_ovenContext.build_root            = __combine(buildRoot, &tmp[0]);
    g_ovenContext.install_root          = __combine(installRoot, &tmp[0]);;
    if (g_ovenContext.build_root == NULL || g_ovenContext.install_root == NULL) {
        free((void*)root);
        free((void*)buildRoot);
        free((void*)installRoot);
        return -1;
    }

    // no active recipe
    g_ovenContext.recipe.name            = NULL;
    g_ovenContext.recipe.relative_path   = NULL;
    g_ovenContext.recipe.build_root      = NULL;
    g_ovenContext.recipe.install_root    = NULL;
    g_ovenContext.recipe.toolchain       = NULL;
    g_ovenContext.recipe.checkpoint_path = NULL;

    // create all paths
    if (__create_path(root) || __create_path(buildRoot) || __create_path(installRoot) ||
        __create_path(g_ovenContext.build_root) || __create_path(g_ovenContext.install_root)) {
        free((void*)root);
        free((void*)buildRoot);
        free((void*)installRoot);
        free((void*)g_ovenContext.build_root);
        free((void*)g_ovenContext.install_root);
        return -1;
    }

    // free the intermediate paths
    free((void*)root);
    free((void*)buildRoot);
    free((void*)installRoot);
    return 0;
}

static int __recreate_dir(const char* path)
{
    int status;

    status = platform_rmdir(path);
    if (status) {
        if (errno != ENOENT) {
            fprintf(stderr, "oven: failed to remove directory: %s\n", strerror(errno));
            return -1;
        }
    }

    status = platform_mkdir(path);
    if (status) {
        fprintf(stderr, "oven: failed to create directory: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int oven_reset(void)
{
    int status;

    status = __recreate_dir(g_ovenContext.build_root);
    if (status) {
        return status;
    }
    
    status = __recreate_dir(g_ovenContext.install_root);
    if (status) {
        return status;
    }
    return 0;
}

int oven_recipe_start(struct oven_recipe_options* options)
{
    char*  buildRoot;
    char*  checkpointPath;
    size_t relativePathLength;
    int    status;

    if (g_ovenContext.recipe.name) {
        fprintf(stderr, "oven: recipe already started\n");
        errno = ENOSYS;
        return -1;
    }

    g_ovenContext.recipe.name          = strdup(options->name);
    g_ovenContext.recipe.relative_path = strdup(options->relative_path);
    g_ovenContext.recipe.toolchain     = options->toolchain != NULL ? strdup(options->toolchain) : NULL;
    
    // generate build and install directories
    buildRoot = malloc(sizeof(char) * (strlen(g_ovenContext.build_root) + strlen(g_ovenContext.recipe.relative_path) + 2));
    if (!buildRoot) {
        errno = ENOMEM;
        return -1;
    }

    relativePathLength = strlen(g_ovenContext.recipe.relative_path);
    if (relativePathLength > 0) {
        if (g_ovenContext.recipe.relative_path[0] != '/') {
            sprintf(buildRoot, "%s/%s", g_ovenContext.build_root, g_ovenContext.recipe.relative_path);
        } else {
            sprintf(buildRoot, "%s%s", g_ovenContext.build_root, g_ovenContext.recipe.relative_path);
        }

        status = platform_mkdir(buildRoot);
        if (status) {
            fprintf(stderr, "oven: failed to create build directory: %s\n", strerror(errno));
            free(buildRoot);
            return status;
        }

    } else {
        strcpy(buildRoot, g_ovenContext.build_root);
    }

    checkpointPath = malloc(strlen(buildRoot) + strlen("/.checkpoints") + 1);
    if (!checkpointPath) {
        errno = ENOMEM;
        free(buildRoot);
        return -1;
    }
    sprintf(checkpointPath, "%s/.checkpoints", buildRoot);

    // store members as const
    g_ovenContext.recipe.build_root      = buildRoot;
    g_ovenContext.recipe.install_root    = strdup(g_ovenContext.install_root);
    g_ovenContext.recipe.checkpoint_path = checkpointPath;
    return 0;
}

void oven_recipe_end(void)
{
    if (g_ovenContext.recipe.name) {
        free((void*)g_ovenContext.recipe.name);
        g_ovenContext.recipe.name = NULL;
    }

    if (g_ovenContext.recipe.relative_path) {
        free((void*)g_ovenContext.recipe.relative_path);
        g_ovenContext.recipe.relative_path = NULL;
    }

    if (g_ovenContext.recipe.toolchain) {
        free((void*)g_ovenContext.recipe.toolchain);
        g_ovenContext.recipe.toolchain = NULL;
    }

    if (g_ovenContext.recipe.build_root) {
        free((void*)g_ovenContext.recipe.build_root);
        g_ovenContext.recipe.build_root = NULL;
    }

    if (g_ovenContext.recipe.install_root) {
        free((void*)g_ovenContext.recipe.install_root);
        g_ovenContext.recipe.install_root = NULL;
    }

    if (g_ovenContext.recipe.checkpoint_path) {
        free((void*)g_ovenContext.recipe.checkpoint_path);
        g_ovenContext.recipe.checkpoint_path = NULL;
    }
}

static int __create_checkpoint(const char* path, const char* checkpoint)
{
    // write checkpoint to file
    FILE* file = fopen(path, "a");
    if (!file) {
        fprintf(stderr, "oven: failed to create checkpoint file: %s\n", strerror(errno));
        return -1;
    }

    fprintf(file, "%s\n", checkpoint);
    fclose(file);
    return 0;
}

static int __has_checkpoint(const char* path, const char* checkpoint)
{
    FILE*   file;
    char*   line = NULL;
    size_t  lineLength = 0;
    ssize_t read;

    // read checkpoint from file
    file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    while ((read = getline(&line, &lineLength, file)) != -1) {
        if (strcmp(line, checkpoint) == 0) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

static const char* __get_variable(const char* name)
{
    if (strcmp(name, "PROJECT_PATH") == 0) {
        printf("PROJECT_PATH: %s\n", g_ovenContext.variables.cwd);
        return g_ovenContext.variables.cwd;
    }
    if (strcmp(name, "INGREDIENTS_PREFIX") == 0) {
        printf("INGREDIENTS_PREFIX: %s\n", g_ovenContext.variables.fridge_prep_directory);
        return g_ovenContext.variables.fridge_prep_directory;
    }
    if (strcmp(name, "TOOLCHAIN_PREFIX") == 0) {
        printf("TOOLCHAIN_PREFIX: %s\n", g_ovenContext.recipe.toolchain);
        return g_ovenContext.recipe.toolchain;
    }
    return NULL;
}

static int __expand_variable(char** at, char** buffer, int* index, size_t* maxLength)
{
    const char* start = *at;
    char*       end   = strchr(start, '}');
    if (end && end[1] == '}') {
        char* variable;

        // fixup at
        *at = (end + 2);

        start += 3; // skip ${{

        // trim leading spaces
        while (*start == ' ') {
            start++;
        }

        // trim trailing spaces
        end--;
        while (*end == ' ') {
            end--;
        }
        end++;
        
        variable = strndup(start, end - start);
        if (variable != NULL) {
            const char* value = __get_variable(variable);
            free(variable);
            if (value != NULL) {
                size_t valueLength = strlen(value);
                if (valueLength > *maxLength) {
                    *maxLength = valueLength;
                    errno = ENOSPC;
                    return -1;
                }
                
                memcpy(&(*buffer)[*index], value, valueLength);
                *index += valueLength;
                return 0;
            } else {
                errno = ENOENT;
                return -1;
            }
        } else {
            errno = ENOMEM;
            return -1;
        }
    }
    errno = EINVAL;
    return -1;
}

static int __expand_environment_variable(char** at, char** buffer, int* index, size_t* maxLength)
{
    const char* start = *at;
    char*       end   = strchr(start, '}');
    if (end) {
        char* variable;
        
        // fixup at
        *at = end + 1;

        start += 2; // skip ${

        // trim leading spaces
        while (*start == ' ') {
            start++;
        }

        // trim trailing spaces
        end--;
        while (*end == ' ') {
            end--;
        }
        end++;

        variable = strndup(start, end - start);
        if (variable != NULL) {
            char* value = getenv(variable);
            free(variable);
            if (value != NULL) {
                size_t valueLength = strlen(value);
                if (valueLength > *maxLength) {
                    *maxLength = valueLength;
                    errno = ENOSPC;
                    return -1;
                }
                
                memcpy(&(*buffer)[*index], value, valueLength);
                *index += valueLength;
                return 0;
            }
        } else {
            errno = ENOENT;
            return -1;
        }
    } else {
        errno = ENOMEM;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

static void* __resize_buffer(void* buffer, size_t length)
{
    void* biggerBuffer = calloc(1, length);
    if (!biggerBuffer) {
        return NULL;
    }
    strcat(biggerBuffer, buffer);
    free(buffer);
    return biggerBuffer;
}

static const char* __preprocess_value(const char* original)
{
    const char* itr = original;
    const char* result;
    char*       buffer;
    size_t      bufferSize = 4096;
    int         index;

    if (original == NULL) {
        return NULL;
    }
    
    buffer = calloc(1, bufferSize);
    if (buffer == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // trim spaces
    while (*itr == ' ') {
        itr++;
    }
    
    index = 0;
    while (*itr) {
        if (strncmp(itr, "${{", 3) == 0) {
            // handle variables
            size_t spaceLeft = bufferSize - index;
            int    status;
            do {
                status = __expand_variable((char**)&itr, &buffer, &index, &spaceLeft);
                if (status) {
                    if (errno == ENOSPC) {
                        buffer = __resize_buffer(buffer, bufferSize + spaceLeft + 1024);
                        if (!buffer) {
                            free(buffer);
                            return NULL;
                        }
                    } else {
                        break;
                    }
                }
            } while (status != 0);
        } else if (strncmp(itr, "${", 2) == 0) {
            // handle environment variables
            size_t spaceLeft = bufferSize - index;
            int    status;
            do {
                status = __expand_environment_variable((char**)&itr, &buffer, &index, &spaceLeft);
                if (status) {
                    if (errno == ENOSPC) {
                        buffer = __resize_buffer(buffer, bufferSize + spaceLeft + 1024);
                        if (!buffer) {
                            free(buffer);
                            return NULL;
                        }
                    } else {
                        break;
                    }
                }
                
            } while (status != 0);
        } else {
            buffer[index++] = *itr;
            itr++;
        }
    }
    
    result = strdup(buffer);
    free(buffer);
    return result;
}

static const char* __build_argument_string(struct list* argumentList)
{
    struct list_item* item;
    char*             argumentString;
    char*             argumentItr;
    size_t            totalLength = 0;

    // allocate memory for the string
    argumentString = (char*)malloc(4096);
    if (argumentString == NULL) {
        return NULL;
    }
    memset(argumentString, 0, 4096);

    // copy arguments into buffer
    argumentItr = argumentString;
    list_foreach(argumentList, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        const char*             valueString = __preprocess_value(value->value);
        size_t                  valueLength = strlen(valueString);

        if (valueLength > 0 && (totalLength + valueLength + 2) < 4096) {
            strcpy(argumentItr, valueString);
            
            totalLength += valueLength;
            argumentItr += valueLength;
            if (item->next) {
                *argumentItr = ' ';
                argumentItr++;
            }
        }
        free((void*)valueString);
    }
    return argumentString;
}

static int __get_project_directory(const char* cwd, const char* projectPath, char** bufferOut)
{
    char* result;

    if (projectPath) {
        size_t cwdLength = strlen(cwd);

        // append the relative path to the root directory
        result = malloc(cwdLength + strlen(projectPath) + 2);
        if (!result) {
            return -1;
        }

        // take into account the trailing slash
        if (cwd[cwdLength] == '/' || projectPath[0] == '/') {
            sprintf(result, "%s%s", cwd, projectPath);
        } else {
            sprintf(result, "%s/%s", cwd, projectPath);
        }
    } else {
        result = strdup(cwd);
        if (!result) {
            return -1;
        }
    }
    
    *bufferOut = result;
    return 0;
}

static struct generate_backend* __get_generate_backend(const char* name)
{
    for (int i = 0; i < sizeof(g_genbackends) / sizeof(struct generate_backend); i++) {
        if (!strcmp(name, g_genbackends[i].name)) {
            return &g_genbackends[i];
        }
    }
    return NULL;
}

static struct oven_keypair_item* __preprocess_keypair(struct oven_keypair_item* original)
{
    struct oven_keypair_item* keypair;

    keypair = (struct oven_keypair_item*)malloc(sizeof(struct oven_keypair_item));
    if (!keypair) {
        return NULL;
    }

    keypair->key   = strdup(original->key);
    keypair->value = __preprocess_value(original->value);
    return keypair;
}

static struct list* __preprocess_keypair_list(struct list* original)
{
    struct list*      processed = malloc(sizeof(struct list));
    struct list_item* item;

    if (!processed) {
        fprintf(stderr, "oven: failed to allocate memory environment preprocessor\n");
        return original;
    }

    list_init(processed);
    list_foreach(original, item) {
        struct oven_keypair_item* keypair          = (struct oven_keypair_item*)item;
        struct oven_keypair_item* processedKeypair = __preprocess_keypair(keypair);
        if (!processedKeypair) {
            fprintf(stderr, "oven: failed to allocate memory environment preprocessor\n");
            break;
        }

        list_add(processed, &processedKeypair->list_header);
    }
    return processed;
}

static void __cleanup_environment(struct list* keypairs)
{
    struct list_item* item;

    if (keypairs == NULL) {
        return;
    }

    for (item = keypairs->head; item != NULL;) {
        struct list_item*         next    = item->next;
        struct oven_keypair_item* keypair = (struct oven_keypair_item*)item;

        free((void*)keypair->key);
        free((void*)keypair->value);
        free(keypair);

        item = next;
    }
    free(keypairs);
}

static void __cleanup_backend_data(struct oven_backend_data* data)
{
    __cleanup_environment(data->environment);
    free((void*)data->arguments);
    free((void*)data->project_directory);
    free((void*)data->root_directory);
}

static int __initialize_backend_data(struct oven_backend_data* data, const char* profile, struct list* arguments, struct list* environment)
{
    int                      status;
    char*                    path;

    // reset the datastructure
    memset(data, 0, sizeof(struct oven_backend_data));

    status = __get_cwd(&path);
    if (status) {
        return status;
    }
    data->root_directory = path;
    
    status = __get_project_directory(data->root_directory, g_ovenContext.recipe.relative_path, &path);
    if (status) {
        free((void*)data->root_directory);
        return status;
    }
    data->project_directory = path;

    data->project_name        = g_ovenContext.recipe.name;
    data->profile_name        = profile != NULL ? profile : "Release";
    data->install_directory   = g_ovenContext.recipe.install_root;
    data->build_directory     = g_ovenContext.recipe.build_root;
    data->process_environment = g_ovenContext.process_environment;
    data->fridge_directory    = g_ovenContext.variables.fridge_prep_directory;
    
    data->environment = __preprocess_keypair_list(environment);
    if (!data->environment) {
        __cleanup_backend_data(data);
        return -1;
    }

    data->arguments = __build_argument_string(arguments);
    if (!data->arguments) {
        __cleanup_backend_data(data);
        return -1;
    }
    return 0;
}

int oven_configure(struct oven_generate_options* options)
{
    struct generate_backend* backend;
    struct oven_backend_data data;
    int                      status;
    char*                    path;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_generate_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    // check if we already have done this step
    if (__has_checkpoint(g_ovenContext.recipe.checkpoint_path, options->system)) {
        return 0;
    }
    
    // build the backend data
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }

    status = backend->generate(&data);
    if (status == 0) {
        status = __create_checkpoint(g_ovenContext.recipe.checkpoint_path, options->system);
    }

cleanup:
    __cleanup_backend_data(&data);
    return status;
}

static struct build_backend* __get_build_backend(const char* name)
{
    for (int i = 0; i < sizeof(g_buildbackends) / sizeof(struct build_backend); i++) {
        if (!strcmp(name, g_buildbackends[i].name)) {
            return &g_buildbackends[i];
        }
    }
    return NULL;
}

int oven_build(struct oven_build_options* options)
{
    struct build_backend*    backend;
    struct oven_backend_data data;
    int                      status;
    char*                    path;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_build_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    // build the backend data
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }

    status = backend->build(&data);

cleanup:
    __cleanup_backend_data(&data);
    return status;
}

void oven_cleanup(void)
{
    if (g_ovenContext.build_root) {
        free((void*)g_ovenContext.build_root);
        g_ovenContext.build_root = NULL;
    }
    
    if (g_ovenContext.install_root) {
        free((void*)g_ovenContext.install_root);
        g_ovenContext.install_root = NULL;
    }

    if (g_ovenContext.variables.cwd) {
        free((void*)g_ovenContext.variables.cwd);
        g_ovenContext.variables.cwd = NULL;
    }
}
