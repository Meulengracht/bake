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
#include <utils.h>

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

#define OVEN_ROOT         ".oven"
#define OVEN_BUILD_ROOT   OVEN_ROOT CHEF_PATH_SEPARATOR_S "build"
#define OVEN_INSTALL_ROOT OVEN_ROOT CHEF_PATH_SEPARATOR_S "install"

struct oven_recipe_context {
    const char* name;
    const char* relative_path;
    const char* toolchain;

    const char* build_root;
    const char* install_root;
    const char* checkpoint_path;
};

struct oven_variables {
    const char* architecture;
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
    int       (*generate)(struct oven_backend_data* data, union oven_backend_options* options);
};

struct build_backend {
    const char* name;
    int       (*build)(struct oven_backend_data* data, union oven_backend_options* options);
};

static struct generate_backend g_genbackends[] = {
    { "configure", configure_main },
    { "cmake",     cmake_main     },
    { "meson",     meson_main     }
};

static struct build_backend g_buildbackends[] = {
    { "make", make_main }
};

static struct oven_context g_ovenContext = { 0 };

const char* __get_build_root(void)
{
    return g_ovenContext.build_root;
}

const char* __get_install_path(void)
{
    return g_ovenContext.install_root;
}

const char* __get_ingredients_path(void)
{
    return g_ovenContext.variables.fridge_prep_directory;
}

const char* __get_architecture(void)
{
    return g_ovenContext.variables.architecture;
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
        // buffer was too small
        fprintf(stderr, "could not get current working directory, buffer too small?\n");
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
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

int oven_initialize(char** envp, char* architecture, const char* recipeScope, const char* fridgePrepDirectory)
{
    int         status;
    char*       cwd;
    const char* root;
    const char* buildRoot;
    const char* installRoot;
    char        tmp[128];
    size_t      length;

    // get the current working directory
    status = __get_cwd(&cwd);
    if (status) {
        return -1;
    }

    // make sure to add the last path seperator if not already
    // present in cwd, we assume space for this due to relatively
    // large buffer of 4096 (this is dangerous assumption!)
    length = strlen(cwd);
    if (cwd[length - 1] != CHEF_PATH_SEPARATOR) {
        cwd[length] = CHEF_PATH_SEPARATOR;
        cwd[length + 1] = '\0';
    }

    // get basename of recipe
    strbasename(recipeScope, tmp, sizeof(tmp));

    // initialize oven paths
    root        = strpathcombine(cwd, OVEN_ROOT);
    buildRoot   = strpathcombine(cwd, OVEN_BUILD_ROOT);
    installRoot = strpathcombine(cwd, OVEN_INSTALL_ROOT);

    if (root == NULL || buildRoot == NULL || installRoot == NULL) {
        free((void*)root);
        free((void*)buildRoot);
        free((void*)installRoot);
        return -1;
    }

    // update oven variables
    g_ovenContext.variables.architecture          = architecture;
    g_ovenContext.variables.cwd                   = cwd;
    g_ovenContext.variables.fridge_prep_directory = fridgePrepDirectory;

    // update oven context
    g_ovenContext.process_environment   = (const char**)envp;
    g_ovenContext.build_root            = strpathcombine(buildRoot, &tmp[0]);
    g_ovenContext.install_root          = strpathcombine(installRoot, &tmp[0]);;
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

int oven_clean(void)
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
    buildRoot = strpathcombine(g_ovenContext.build_root, options->relative_path);
    if (!buildRoot) {
        errno = ENOMEM;
        return -1;
    }

    status = platform_mkdir(buildRoot);
    if (status) {
        fprintf(stderr, "oven: failed to create build directory: %s\n", strerror(errno));
        free(buildRoot);
        return status;
    }

    checkpointPath = strpathcombine(buildRoot, ".checkpoints");
    if (!checkpointPath) {
        errno = ENOMEM;
        free(buildRoot);
        return -1;
    }

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

int oven_clear_recipe_checkpoint(const char* name)
{
    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (g_ovenContext.recipe.checkpoint_path == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return oven_checkpoint_remove(g_ovenContext.recipe.checkpoint_path, name);
}

static const char* __get_variable(const char* name)
{
    if (strcmp(name, "CHEF_ARCHITECTURE") == 0) {
        printf("CHEF_ARCHITECTURE: %s\n", g_ovenContext.variables.architecture);
        return g_ovenContext.variables.architecture;
    }
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
    if (strcmp(name, "INSTALL_PREFIX") == 0) {
        printf("INSTALL_PREFIX: %s\n", g_ovenContext.recipe.install_root);
        return g_ovenContext.recipe.install_root;
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

const char* __build_argument_string(struct list* argumentList)
{
    struct list_item* item;
    char*             argumentString;
    char*             argumentItr;
    size_t            totalLength = 0;

    // allocate memory for the string
    argumentString = (char*)malloc(4096);
    if (argumentString == NULL) {
        errno = ENOMEM;
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
    
    path = strpathcombine(data->root_directory, g_ovenContext.recipe.relative_path);
    if (path == NULL) {
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
    if (oven_checkpoint_contains(g_ovenContext.recipe.checkpoint_path, options->name)) {
        printf("nothing to be done for %s\n", options->name);
        return 0;
    }
    
    printf("running step %s\n", options->name);
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }

    status = backend->generate(&data, options->system_options);
    if (status == 0) {
        status = oven_checkpoint_create(g_ovenContext.recipe.checkpoint_path, options->name);
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

    // check if we already have done this step
    if (oven_checkpoint_contains(g_ovenContext.recipe.checkpoint_path, options->name)) {
        printf("nothing to be done for %s\n", options->name);
        return 0;
    }

    printf("running step %s\n", options->name);
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }

    status = backend->build(&data, options->system_options);
    if (status == 0) {
        status = oven_checkpoint_create(g_ovenContext.recipe.checkpoint_path, options->name);
    }

cleanup:
    __cleanup_backend_data(&data);
    return status;
}

int oven_script(struct oven_script_options* options)
{
    const char* preprocessedScript;
    int         status;

    // handle script substitution first, then we pass it on
    // to the platform handler
    if (options == NULL || options->script == NULL) {
        errno = EINVAL;
        return -1;
    }

    // check if we already have done this step
    if (oven_checkpoint_contains(g_ovenContext.recipe.checkpoint_path, options->name)) {
        printf("nothing to be done for %s\n", options->name);
        return 0;
    }

    printf("running step %s\n", options->name);
    preprocessedScript = __preprocess_value(options->script);
    if (preprocessedScript == NULL) {
        return -1;
    }

    status = platform_script(preprocessedScript);
    free((void*)preprocessedScript);

    if (status == 0) {
        status = oven_checkpoint_create(g_ovenContext.recipe.checkpoint_path, options->name);
    }
    return status;
}

static int __copy_file(const char* source, const char* destination)
{
    int    status;
    FILE*  sourceFile;
    FILE*  destinationFile;
    char*  buffer;
    size_t fileSize;

    sourceFile = fopen(source, "rb");
    if (!sourceFile) {
        return -1;
    }

    fseek(sourceFile, 0, SEEK_END);
    fileSize = ftell(sourceFile);
    fseek(sourceFile, 0, SEEK_SET);

    buffer = (char*)malloc(fileSize);
    if (!buffer) {
        fclose(sourceFile);
        return -1;
    }

    if (fread(buffer, 1, fileSize, sourceFile) != fileSize) {
        free(buffer);
        fclose(sourceFile);
        return -1;
    }
    fclose(sourceFile);

    destinationFile = fopen(destination, "wb");
    if (!destinationFile) {
        fclose(sourceFile);
        return -1;
    }

    if (fwrite(buffer, 1, fileSize, destinationFile) != fileSize) {
        free(buffer);
        fclose(destinationFile);
        return -1;
    }

    fclose(destinationFile);
    return status;
}

static int __matches_filters(const char* path, struct list* filters)
{
    struct list_item* item;

    if (filters->count == 0) {
        return 0; // YES! no filters means everything matches
    }

    list_foreach(filters, item) {
        struct oven_value_item* filter = (struct oven_value_item*)item;
        if (strfilter(filter->value, path, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

int __copy_files_with_filters(const char* sourceRoot, const char* path, struct list* filters, const char* destinationRoot)
{
    // recursively iterate through the directory and copy all files
    // as long as they match the list of filters
    int            status = -1;
    struct dirent* entry;
    DIR*           dir;
    const char*    finalSource;
    const char*    finalDestination = NULL;
    
    finalSource = strpathcombine(sourceRoot, path);
    if (!finalSource) {
        goto cleanup;
    }

    finalDestination = strpathcombine(destinationRoot, path);
    if (!finalDestination) {
        goto cleanup;
    }

    dir = opendir(finalSource);
    if (!dir) {
        goto cleanup;
    }

    // make sure target is created
    if (platform_mkdir(finalDestination)) {
        goto cleanup;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char* combinedSubPath;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        combinedSubPath = strpathcombine(path, entry->d_name);
        if (!combinedSubPath) {
            goto cleanup;
        }

        // does this match filters?
        if (__matches_filters(combinedSubPath, filters)) {
            free((void*)combinedSubPath);
            continue;
        }

        // oh ok, is it a directory?
        if (entry->d_type == DT_DIR) {
            status = __copy_files_with_filters(sourceRoot, combinedSubPath, filters, destinationRoot);
            free((void*)combinedSubPath);
            if (status) {
                goto cleanup;
            }
        } else {
            // ok, it's a file, copy it
            char* sourceFile      = strpathcombine(finalSource, entry->d_name);
            char* destinationFile = strpathcombine(finalDestination, entry->d_name);
            free((void*)combinedSubPath);
            if (!sourceFile || !destinationFile) {
                free((void*)sourceFile);
                goto cleanup;
            }

            status = __copy_file(sourceFile, destinationFile);
            free((void*)sourceFile);
            free((void*)destinationFile);
            if (status) {
                goto cleanup;
            }
        }
    }

    closedir(dir);
    status = 0;

cleanup:
    free((void*)finalSource);
    free((void*)finalDestination);
    return status;
}

int oven_include_filters(struct list* filters)
{
    if (!filters) {
        errno = EINVAL;
        return -1;
    }

    return __copy_files_with_filters(
        g_ovenContext.variables.fridge_prep_directory,
        NULL,
        filters,
        g_ovenContext.recipe.install_root
    );
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
