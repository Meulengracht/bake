/**
 * Copyright 2024, Philip Meulengracht
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

#include <ctype.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <yaml/yaml.h>
#include <stdio.h>
#include <stdlib.h>

extern int recipe_postprocess(struct recipe* recipe);

enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_PROJECT,
    STATE_PROJECT_NAME,
    STATE_PROJECT_SUMMARY,
    STATE_PROJECT_DESCRIPTION,
    STATE_PROJECT_ICON,
    STATE_PROJECT_AUTHOR,
    STATE_PROJECT_EMAIL,
    STATE_PROJECT_VERSION,
    STATE_PROJECT_LICENSE,
    STATE_PROJECT_EULA,
    STATE_PROJECT_HOMEPAGE,

    STATE_ENVIRONMENT,
    STATE_ENVIRONMENT_HOST,
    STATE_ENVIRONMENT_BUILD,
    STATE_ENVIRONMENT_RUNTIME,
    STATE_ENVIRONMENT_HOOKS,

    STATE_ENVIRONMENT_HOST_BASE,
    STATE_ENVIRONMENT_HOST_PACKAGES_LIST,

    STATE_ENVIRONMENT_BUILD_CONFINEMENT,

    STATE_ENVIRONMENT_HOOKS_BASH,
    STATE_ENVIRONMENT_HOOKS_POWERSHELL,

    STATE_PLATFORM_LIST,

    STATE_PLATFORM,       // MAPPING_START
    STATE_PLATFORM_NAME,
    STATE_PLATFORM_TOOLCHAIN,
    STATE_PLATFORM_ARCH_LIST,

    STATE_INGREDIENT_LIST,

    STATE_INGREDIENT,       // MAPPING_START
    STATE_INGREDIENT_NAME,
    STATE_INGREDIENT_VERSION,
    STATE_INGREDIENT_INCLUDE_FILTERS_LIST,
    STATE_INGREDIENT_CHANNEL,

    STATE_RECIPE_LIST,
    STATE_RECIPE,          // MAPPING_START
    STATE_RECIPE_NAME,
    STATE_RECIPE_SOURCE,
    STATE_RECIPE_TOOLCHAIN,

    STATE_RECIPE_SOURCE_TYPE,
    STATE_RECIPE_SOURCE_SCRIPT,
    STATE_RECIPE_SOURCE_PATH,
    STATE_RECIPE_SOURCE_URL,
    STATE_RECIPE_SOURCE_GIT_REPO,
    STATE_RECIPE_SOURCE_GIT_BRANCH,
    STATE_RECIPE_SOURCE_GIT_COMMIT,

    STATE_RECIPE_STEP_LIST,
    STATE_RECIPE_STEP,     // MAPPING_START
    STATE_RECIPE_STEP_NAME,
    STATE_RECIPE_STEP_TYPE,
    STATE_RECIPE_STEP_DEPEND_LIST,
    STATE_RECIPE_STEP_SYSTEM,
    STATE_RECIPE_STEP_SCRIPT,
    STATE_RECIPE_STEP_ARGUMENT_LIST,

    STATE_RECIPE_STEP_MESON_CROSS_FILE,
    STATE_RECIPE_STEP_MESON_WRAPS_LIST,

    STATE_MESON_WRAP,
    STATE_MESON_WRAP_NAME,
    STATE_MESON_WRAP_INGREDIENT,

    STATE_RECIPE_STEP_MAKE_INTREE,
    STATE_RECIPE_STEP_MAKE_PARALLEL,

    STATE_RECIPE_STEP_ENV_LIST_KEY,
    STATE_RECIPE_STEP_ENV_LIST_VALUE,

    STATE_PACKS_LIST,
    STATE_PACK,            // MAPPING_START
    STATE_PACK_NAME,
    STATE_PACK_TYPE,
    STATE_PACK_INGREDIENT_OPTIONS,
    STATE_PACK_FILTER_LIST,
    STATE_PACK_COMMANDS_LIST,

    STATE_PACK_INGREDIENT_OPTIONS_BIN_PATHS_LIST,
    STATE_PACK_INGREDIENT_OPTIONS_INC_PATHS_LIST,
    STATE_PACK_INGREDIENT_OPTIONS_LIB_PATHS_LIST,
    STATE_PACK_INGREDIENT_OPTIONS_COMPILER_ARGS_LIST,
    STATE_PACK_INGREDIENT_OPTIONS_LINKER_ARGS_LIST,

    STATE_COMMAND,         // MAPPING_START
    STATE_COMMAND_NAME,
    STATE_COMMAND_PATH,
    STATE_COMMAND_ARGUMENT_LIST,
    STATE_COMMAND_TYPE,
    STATE_COMMAND_ICON,
    STATE_COMMAND_SYSTEMLIBS,
    STATE_COMMAND_DESCRIPTION,

    STATE_STOP
};

struct parser_state {
    enum state                  states[32];
    int                         state_index;
    enum state                  state;
    struct list*                ingredients;
    enum recipe_ingredient_type ingredients_type;
    struct recipe               recipe;
    struct recipe_platform      platform;
    struct recipe_ingredient    ingredient;
    struct recipe_part          part;
    struct recipe_step          step;
    struct recipe_pack          pack;
    struct recipe_pack_command  command;
    struct chef_keypair_item    env_keypair;
    struct meson_wrap_item      meson_wrap_item;
};

static void __parser_push_state(struct parser_state* state, enum state next) {
    state->states[state->state_index++] = state->state;
    state->state = next;
}

static void __parser_pop_state(struct parser_state* state) {
    state->state = state->states[--state->state_index];
}

static const char* __parse_string(const char* value)
{
    if (value == NULL || strlen(value) == 0) {
        return NULL;
    }

    return platform_strdup(value);
}

static enum chef_package_type __parse_pack_type(const char* value)
{
    if (strcmp(value, "bootloader") == 0) {
        return CHEF_PACKAGE_TYPE_BOOTLOADER;
    } else if (strcmp(value, "os") == 0) {
        return CHEF_PACKAGE_TYPE_OSBASE;
    } else if (strcmp(value, "ingredient") == 0) {
        return CHEF_PACKAGE_TYPE_INGREDIENT;
    } else if (strcmp(value, "content") == 0) {
        return CHEF_PACKAGE_TYPE_CONTENT;
    } else if (strcmp(value, "application") == 0) {
        return CHEF_PACKAGE_TYPE_APPLICATION;
    } else if (strcmp(value, "toolchain") == 0) {
        return CHEF_PACKAGE_TYPE_TOOLCHAIN;
    } else {
        return CHEF_PACKAGE_TYPE_UNKNOWN;
    }
}

static enum recipe_part_source_type __parse_recipe_part_source_type(const char* value)
{
    if (value == NULL || strlen(value) == 0) {
        return RECIPE_PART_SOURCE_TYPE_PATH;
    }

    if (strcmp(value, "git") == 0) {
        return RECIPE_PART_SOURCE_TYPE_GIT;
    } else if (strcmp(value, "url") == 0) {
        return RECIPE_PART_SOURCE_TYPE_URL;
    } else {
        return RECIPE_PART_SOURCE_TYPE_PATH;
    }
}

static enum recipe_step_type __parse_recipe_step_type(const char* value)
{
    if (strcmp(value, "generate") == 0) {
        return RECIPE_STEP_TYPE_GENERATE;
    } else if (strcmp(value, "build") == 0) {
        return RECIPE_STEP_TYPE_BUILD;
    } else if (strcmp(value, "script") == 0) {
        return RECIPE_STEP_TYPE_SCRIPT;
    } else {
        return RECIPE_STEP_TYPE_UNKNOWN;
    }
}

static enum chef_command_type __parse_command_type(const char* value)
{
    if (strcmp(value, "executable") == 0) {
        return CHEF_COMMAND_TYPE_EXECUTABLE;
    } else if (strcmp(value, "daemon") == 0) {
        return CHEF_COMMAND_TYPE_DAEMON;
    } else {
        return CHEF_COMMAND_TYPE_UNKNOWN;
    }
}

static void __finalize_recipe(struct parser_state* state)
{
    // todo
}

static int __is_valid_name(const char* name)
{
    if (name == NULL || strlen(name) == 0) {
        return -1;
    }

    // step names must only contain a-z and '-_'
    while (*name != '\0') {
        if (!(isascii(*name) || (*name == '_') || (*name == '-'))) {
            return -1;
        }
        name++;
    }
    return 0;
}

static void __finalize_project(struct parser_state* state)
{
    // verify required project members
    if (__is_valid_name(state->recipe.project.name)) {
        fprintf(stderr, "parse error: project name must be provided and only contain [a-zA-Z_-]\n");
        exit(EXIT_FAILURE);
    }

    if (state->recipe.project.summary == NULL) {
        fprintf(stderr, "parse error: project summary is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->recipe.project.version == NULL) {
        fprintf(stderr, "parse error: project version must be specified\n");
        exit(EXIT_FAILURE);
    }

    if (state->recipe.project.author == NULL) {
        fprintf(stderr, "parse error: project author is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->recipe.project.email == NULL) {
        fprintf(stderr, "parse error: project author email is required\n");
        exit(EXIT_FAILURE);
    }
}

static void __finalize_platform(struct parser_state* state)
{
    struct recipe_platform* platform;

    if (state->platform.name == NULL) {
        fprintf(stderr, "parse error: platform name is required\n");
        exit(EXIT_FAILURE);
    }
    
    // now we copy and reset
    platform = malloc(sizeof(struct recipe_platform));
    if (platform == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    // copy the set values
    memcpy(platform, &state->platform, sizeof(struct recipe_platform));
    list_add(&state->recipe.platforms, &platform->list_header);

    // reset the structure in state
    memset(&state->platform, 0, sizeof(struct recipe_platform));
}

static void __finalize_ingredient(struct parser_state* state)
{
    struct recipe_ingredient* ingredient;

    // we should verify required members of the ingredient before creating a copy
    if (state->ingredient.name == NULL) {
        fprintf(stderr, "parse error: ingredient name is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->ingredient.channel == NULL) {
        fprintf(stderr, "parse error: ingredient %s: channel is required\n", state->ingredient.name);
        exit(EXIT_FAILURE);
    }

    // update the type
    state->ingredient.type = state->ingredients_type;

    // now we copy and reset
    ingredient = malloc(sizeof(struct recipe_ingredient));
    if (ingredient == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    // copy the set values
    memcpy(ingredient, &state->ingredient, sizeof(struct recipe_ingredient));
    list_add(state->ingredients, &ingredient->list_header);

    // reset the structure in state
    memset(&state->ingredient, 0, sizeof(struct recipe_ingredient));
}

static void __finalize_part(struct parser_state* state)
{
    struct recipe_part* part;

    // we should verify required members of the part before creating a copy
    if (__is_valid_name(state->part.name)) {
        fprintf(stderr, "parse error: part name must be provided and only contain [a-zA-Z_-]\n");
        exit(EXIT_FAILURE);
    }
    
    switch (state->part.source.type) {
        case RECIPE_PART_SOURCE_TYPE_URL:
            if (state->part.source.url.url == NULL) {
                fprintf(stderr, "parse error: recipe %s: url is required\n", state->part.name);
                exit(EXIT_FAILURE);
            }
            break;
        case RECIPE_PART_SOURCE_TYPE_GIT:
            if (state->part.source.git.url == NULL) {
                fprintf(stderr, "parse error: recipe %s: git repository url is required\n", state->part.name);
                exit(EXIT_FAILURE);
            }
            break;
            
        default:
            break;
    }
    
    // now we copy and reset
    part = malloc(sizeof(struct recipe_part));
    if (part == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(part, &state->part, sizeof(struct recipe_part));
    list_add(&state->recipe.parts, &part->list_header);

    // reset the structure in state
    memset(&state->part, 0, sizeof(struct recipe_part));
}

static int __find_step(struct parser_state* state, const char* name)
{
    struct list_item* item;

    // go through dependency list and make sure we can find the step
    list_foreach(&state->part.steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        if (strcmp(step->name, name) == 0) {
            return 0;
        }
    }
    return -1;
}

static int __resolve_step_dependencies(struct parser_state* state, struct list* dependencies)
{
    struct list_item* item;

    // go through dependency list and make sure we can find the step
    list_foreach(dependencies, item) {
        struct list_item_string* value = (struct list_item_string*)item;
        if (__find_step(state, value->value)) {
            fprintf(stderr, "parse error: step %s which does not exist\n", value->value);
            return -1;
        }
    }
    return 0;
}

static void __finalize_step(struct parser_state* state)
{
    struct recipe_step* step;
    int                 status;

    // we should verify required members of the step before creating a copy
    if (__is_valid_name(state->step.name)) {
        fprintf(stderr, "parse error: part %s: step name must be provided and only contain [a-zA-Z_-]\n", state->part.name);
        exit(EXIT_FAILURE);
    }

    if (state->step.type == RECIPE_STEP_TYPE_UNKNOWN) {
        fprintf(stderr, "parse error: part %s: step %s: valid step types are {generate, build, script}\n",
            state->part.name, state->step.name);
        exit(EXIT_FAILURE);
    }

    if (state->step.type != RECIPE_STEP_TYPE_SCRIPT && state->step.system == NULL) {
        fprintf(stderr, "parse error: part %s: step %s: system is required\n",
            state->part.name, state->step.name);
        exit(EXIT_FAILURE);
    }

    // verify dependencies are defined
    status = __resolve_step_dependencies(state, &state->step.depends);
    if (status != 0) {
        fprintf(stderr, "parse error: part %s: step %s: dependencies could not be resolved\n",
            state->part.name, state->step.name);
        exit(EXIT_FAILURE);
    }
    
    // now we copy and reset
    step = malloc(sizeof(struct recipe_step));
    if (step == NULL) {
        fprintf(stderr, "parse error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(step, &state->step, sizeof(struct recipe_step));
    list_add(&state->part.steps, &step->list_header);

    // reset the structure in state
    memset(&state->step, 0, sizeof(struct recipe_step));
}

static void __finalize_step_env(struct parser_state* state)
{
    struct chef_keypair_item* keypair;

    // key value must be provided
    if (state->env_keypair.key == NULL || strlen(state->env_keypair.key) == 0) {
        return;
    }

    keypair = malloc(sizeof(struct chef_keypair_item));
    if (keypair == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    keypair->key   = state->env_keypair.key;
    keypair->value = state->env_keypair.value;
    list_add(&state->step.env_keypairs, &keypair->list_header);

    // reset the keypair
    state->env_keypair.key   = NULL;
    state->env_keypair.value = NULL;
}

static void __finalize_command(struct parser_state* state)
{
    struct recipe_pack_command* command;

    // we should verify required members of the command before creating a copy
    if (__is_valid_name(state->command.name)) {
        fprintf(stderr, "parse error: command name must be provided and only contain [a-zA-Z_-]\n");
        exit(EXIT_FAILURE);
    }

    if (state->command.type == CHEF_COMMAND_TYPE_UNKNOWN) {
        fprintf(stderr, "parse error: command %s: valid command types are {executable, daemon}\n", state->command.name);
        exit(EXIT_FAILURE);
    }

    if (state->command.path == NULL) {
        fprintf(stderr, "parse error: command %s: path is required\n", state->command.name);
        exit(EXIT_FAILURE);
    }
    
    // now we copy and reset
    command = malloc(sizeof(struct recipe_pack_command));
    if (command == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(command, &state->command, sizeof(struct recipe_pack_command));
    list_add(&state->pack.commands, &command->list_header);

    // reset the structure in state
    memset(&state->command, 0, sizeof(struct recipe_pack_command));
}

static void __finalize_pack_ingredient_options(struct parser_state* state)
{
    // todo
}

static void __finalize_pack(struct parser_state* state)
{
    struct recipe_pack* pack;

    // we should verify required members of the command before creating a copy
    if (state->pack.name == NULL) {
        fprintf(stderr, "parse error: pack name is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->pack.type == CHEF_PACKAGE_TYPE_UNKNOWN) {
        fprintf(stderr, "parse error: pack type is not specified\n");
        exit(EXIT_FAILURE);
    }

    // commands are only allowed in application mode
    if (state->pack.type != CHEF_PACKAGE_TYPE_APPLICATION && state->pack.commands.count != 0) {
        fprintf(stderr, "parse error: pack %s: commands are only allowed in application packs\n", state->pack.name);
        exit(EXIT_FAILURE);
    }

    // now we copy and reset
    pack = malloc(sizeof(struct recipe_pack));
    if (pack == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(pack, &state->pack, sizeof(struct recipe_pack));
    list_add(&state->recipe.packs, &pack->list_header);

    // reset the structure in state
    memset(&state->pack, 0, sizeof(struct recipe_pack));
}

static int __resolve_ingredient(struct parser_state* state, const char* name)
{
    struct list_item* i;

    list_foreach(&state->recipe.environment.build.ingredients, i) {
        struct recipe_ingredient* ing = (struct recipe_ingredient*)i;
        if (strcmp(ing->name, name) == 0) {
            return 1;
        }
    }
    list_foreach(&state->recipe.environment.runtime.ingredients, i) {
        struct recipe_ingredient* ing = (struct recipe_ingredient*)i;
        if (strcmp(ing->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void __finalize_meson_wrap_item(struct parser_state* state)
{
    struct meson_wrap_item* wrapItem;

    if (state->meson_wrap_item.name == NULL) {
        fprintf(stderr, "parse error: meson wrap name is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->meson_wrap_item.ingredient == NULL) {
        fprintf(stderr, "parse error: meson wrap ingredient is required\n");
        exit(EXIT_FAILURE);
    }

    // verify that we can resolve the ingredient being mentioned
    if (!__resolve_ingredient(state, state->meson_wrap_item.ingredient)) {
        fprintf(stderr, "parse error: ingredient %s specified by meson wrap is not defined\n", state->meson_wrap_item.ingredient);
        exit(EXIT_FAILURE);
    }
    
    // now we copy and reset
    wrapItem = malloc(sizeof(struct meson_wrap_item));
    if (wrapItem == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(wrapItem, &state->meson_wrap_item, sizeof(struct meson_wrap_item));
    list_add(&state->step.options.meson.wraps, &wrapItem->list_header);

    // reset the structure in state
    memset(&state->meson_wrap_item, 0, sizeof(struct meson_wrap_item));
}

// TODO error handling
#define DEFINE_LIST_STRING_ADD(_fname, _stname, _field) \
    static void __add_##_fname ##_##_field(struct parser_state* state, const char* value) \
    { \
        struct list_item_string* argument; \
        if (value == NULL || strlen(value) == 0) { \
            return; \
        } \
        \
        argument = malloc(sizeof(struct list_item_string)); \
        if (!argument) { \
            fprintf(stderr, "error: out of memory\n"); \
            exit(EXIT_FAILURE); \
        } \
        \
        argument->value = platform_strdup(value); \
        \
        list_add(&state->_stname._field, &argument->list_header); \
    }

DEFINE_LIST_STRING_ADD(recipe, recipe.environment.host, packages)
DEFINE_LIST_STRING_ADD(platform, platform, archs)
DEFINE_LIST_STRING_ADD(ingredient, ingredient, filters)
DEFINE_LIST_STRING_ADD(step, step, depends)
DEFINE_LIST_STRING_ADD(step, step, arguments)
DEFINE_LIST_STRING_ADD(pack, pack, filters)
DEFINE_LIST_STRING_ADD(pack_options, pack.options, bin_dirs)
DEFINE_LIST_STRING_ADD(pack_options, pack.options, inc_dirs)
DEFINE_LIST_STRING_ADD(pack_options, pack.options, lib_dirs)
DEFINE_LIST_STRING_ADD(pack_options, pack.options, compiler_flags)
DEFINE_LIST_STRING_ADD(pack_options, pack.options, linker_flags)
DEFINE_LIST_STRING_ADD(command, command, arguments)

static int __parse_boolean(const char* string)
{
    char*  t[] = {"y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON", NULL};
    char*  f[] = {"n", "N", "no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF", NULL};
    char** p;

    for (p = t; *p; p++) {
        if (strcmp(string, *p) == 0) {
            return 1;
        }
    }
    for (p = f; *p; p++) {
        if (strcmp(string, *p) == 0) {
            return 0;
        }
    }

    // default to false
    fprintf(stderr, "parse error: unrecognized boolean value: %s\n", string);
    return 0;
}

static int __consume_event(struct parser_state* s, yaml_event_t* event)
{
    char *value;
    //printf("__consume_event(state=%d event=%d)\n", s->state, event->type);

    switch (s->state) {
        case STATE_START:
            switch (event->type) {
                case YAML_STREAM_START_EVENT:
                    __parser_push_state(s, STATE_STREAM);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_STREAM:
            switch (event->type) {
                case YAML_DOCUMENT_START_EVENT:
                    __parser_push_state(s, STATE_DOCUMENT);
                    break;
                case YAML_STREAM_END_EVENT:
                    __parser_push_state(s, STATE_STOP);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DOCUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    __parser_push_state(s, STATE_SECTION);
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_SECTION:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "project") == 0) {
                        __parser_push_state(s, STATE_PROJECT);
                    } else if (strcmp(value, "environment") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT);
                    } else if (strcmp(value, "platforms") == 0) {
                        __parser_push_state(s, STATE_PLATFORM_LIST);
                    } else if (strcmp(value, "recipes") == 0) {
                        __parser_push_state(s, STATE_RECIPE_LIST);
                    } else if (strcmp(value, "packs") == 0) {
                        __parser_push_state(s, STATE_PACKS_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_SECTION) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                
                case YAML_MAPPING_END_EVENT:
                    __finalize_recipe(s);
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_PROJECT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_project(s);
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_PROJECT_NAME);
                    } else if (strcmp(value, "summary") == 0) {
                        __parser_push_state(s, STATE_PROJECT_SUMMARY);
                    } else if (strcmp(value, "description") == 0) {
                        __parser_push_state(s, STATE_PROJECT_DESCRIPTION);
                    } else if (strcmp(value, "icon") == 0) {
                        __parser_push_state(s, STATE_PROJECT_ICON);
                    } else if (strcmp(value, "author") == 0) {
                        __parser_push_state(s, STATE_PROJECT_AUTHOR);
                    } else if (strcmp(value, "email") == 0) {
                        __parser_push_state(s, STATE_PROJECT_EMAIL);
                    } else if (strcmp(value, "version") == 0) {
                        __parser_push_state(s, STATE_PROJECT_VERSION);
                    } else if (strcmp(value, "license") == 0) {
                        __parser_push_state(s, STATE_PROJECT_LICENSE);
                    } else if (strcmp(value, "eula") == 0) {
                        __parser_push_state(s, STATE_PROJECT_EULA);
                    } else if (strcmp(value, "homepage") == 0) {
                        __parser_push_state(s, STATE_PROJECT_HOMEPAGE);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PROJECT) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

#define __consume_scalar_fn(__STATE, __FIELD, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SCALAR_EVENT: \
                    value = (char *)event->data.scalar.value; \
                    s->__FIELD = __FN(value); \
                    __parser_pop_state(s); \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_system_option_scalar_fn(__STATE, __SYSTEM, __FIELD, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SCALAR_EVENT: \
                    if (s->step.system == NULL || strcmp(s->step.system, __SYSTEM) != 0) {\
                        fprintf(stderr, "unexpected option: " #__STATE ".\n"); \
                        fprintf(stderr, "system options must appear after 'system' keyword\n"); \
                        return -1; \
                    } \
                    value = (char *)event->data.scalar.value; \
                    s->step.options.__FIELD = __FN(value); \
                    __parser_pop_state(s); \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_sequence_mapped(__LISTSTATE, __ITEMSTATE) \
        case __LISTSTATE: \
            switch (event->type) { \
                case YAML_SEQUENCE_START_EVENT: \
                    break; \
                case YAML_SEQUENCE_END_EVENT: \
                    __parser_pop_state(s); \
                    break; \
                case YAML_MAPPING_START_EVENT: \
                    __parser_push_state(s, __ITEMSTATE); \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_sequence_unmapped(__STATE, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SEQUENCE_START_EVENT: \
                    break; \
                case YAML_SEQUENCE_END_EVENT: \
                    __parser_pop_state(s); \
                    break; \
                case YAML_SCALAR_EVENT: \
                    __FN(s, (char *)event->data.scalar.value); \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;


        __consume_scalar_fn(STATE_PROJECT_NAME, recipe.project.name, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_SUMMARY, recipe.project.summary, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_DESCRIPTION, recipe.project.description, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_ICON, recipe.project.icon, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_AUTHOR, recipe.project.author, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_EMAIL, recipe.project.email, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_VERSION, recipe.project.version, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_LICENSE, recipe.project.license, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_EULA, recipe.project.eula, __parse_string)
        __consume_scalar_fn(STATE_PROJECT_HOMEPAGE, recipe.project.url, __parse_string)

        __consume_sequence_mapped(STATE_PLATFORM_LIST, STATE_PLATFORM)

        case STATE_PLATFORM:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_PLATFORM_NAME);
                    } else if (strcmp(value, "toolchain") == 0) {
                        __parser_push_state(s, STATE_PLATFORM_TOOLCHAIN);
                    } else if (strcmp(value, "architectures") == 0) {
                        __parser_push_state(s, STATE_PLATFORM_ARCH_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PLATFORM) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_platform(s);
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_PLATFORM_NAME, platform.name, __parse_string)
        __consume_scalar_fn(STATE_PLATFORM_TOOLCHAIN, platform.toolchain, __parse_string)
        __consume_sequence_unmapped(STATE_PLATFORM_ARCH_LIST, __add_platform_archs)

        case STATE_ENVIRONMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "host") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOST);
                    } else if (strcmp(value, "build") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_BUILD);
                    } else if (strcmp(value, "runtime") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_RUNTIME);
                    } else if (strcmp(value, "hooks") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOOKS);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_ENVIRONMENT) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_ENVIRONMENT_HOST:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "base") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOST_BASE);
                    } else if (strcmp(value, "ingredients") == 0) {
                        s->ingredients_type = RECIPE_INGREDIENT_TYPE_HOST;
                        s->ingredients = &s->recipe.environment.host.ingredients;
                        __parser_push_state(s, STATE_INGREDIENT_LIST);
                    } else if (strcmp(value, "packages") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOST_PACKAGES_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_ENVIRONMENT_HOST) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_ENVIRONMENT_HOST_BASE, recipe.environment.host.base, __parse_boolean)
        __consume_sequence_unmapped(STATE_ENVIRONMENT_HOST_PACKAGES_LIST, __add_recipe_packages)

        case STATE_ENVIRONMENT_BUILD:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "confinement") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_BUILD_CONFINEMENT);
                    } else if (strcmp(value, "ingredients") == 0) {
                        s->ingredients_type = RECIPE_INGREDIENT_TYPE_BUILD;
                        s->ingredients = &s->recipe.environment.build.ingredients;
                        __parser_push_state(s, STATE_INGREDIENT_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_ENVIRONMENT_BUILD) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_ENVIRONMENT_BUILD_CONFINEMENT, recipe.environment.build.confinement, __parse_boolean)

        case STATE_ENVIRONMENT_RUNTIME:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "ingredients") == 0) {
                        s->ingredients_type = RECIPE_INGREDIENT_TYPE_RUNTIME;
                        s->ingredients = &s->recipe.environment.runtime.ingredients;
                        __parser_push_state(s, STATE_INGREDIENT_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_ENVIRONMENT_RUNTIME) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;


        case STATE_ENVIRONMENT_HOOKS:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "bash") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOOKS_BASH);
                    } else if (strcmp(value, "powershell") == 0) {
                        __parser_push_state(s, STATE_ENVIRONMENT_HOOKS_POWERSHELL);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_ENVIRONMENT_HOOKS) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_ENVIRONMENT_HOOKS_BASH, recipe.environment.hooks.bash, __parse_string)
        __consume_scalar_fn(STATE_ENVIRONMENT_HOOKS_POWERSHELL, recipe.environment.hooks.powershell, __parse_string)

        __consume_sequence_mapped(STATE_INGREDIENT_LIST, STATE_INGREDIENT)

        case STATE_INGREDIENT:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_INGREDIENT_NAME);
                    } else if (strcmp(value, "channel") == 0) {
                        __parser_push_state(s, STATE_INGREDIENT_CHANNEL);
                    } else if (strcmp(value, "version") == 0) {
                        __parser_push_state(s, STATE_INGREDIENT_VERSION);
                    } else if (strcmp(value, "include-filters") == 0) {
                        __parser_push_state(s, STATE_INGREDIENT_INCLUDE_FILTERS_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_INGREDIENT) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_ingredient(s);
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_INGREDIENT_NAME, ingredient.name, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT_CHANNEL, ingredient.channel, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT_VERSION, ingredient.version, __parse_string)
        __consume_sequence_unmapped(STATE_INGREDIENT_INCLUDE_FILTERS_LIST, __add_ingredient_filters)

        __consume_sequence_mapped(STATE_RECIPE_LIST, STATE_RECIPE)

        case STATE_RECIPE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_part(s);
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_RECIPE_NAME);
                    } else if (strcmp(value, "source") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE);
                    } else if (strcmp(value, "toolchain") == 0) {
                        __parser_push_state(s, STATE_RECIPE_TOOLCHAIN);
                    } else if (strcmp(value, "steps") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_RECIPE) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_RECIPE_NAME, part.name, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_TOOLCHAIN, part.toolchain, __parse_string)

        case STATE_RECIPE_SOURCE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_TYPE);
                    } else if (strcmp(value, "url") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_URL);
                    } else if (strcmp(value, "path") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_PATH);
                    } else if (strcmp(value, "git-url") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_GIT_REPO);
                    } else if (strcmp(value, "git-branch") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_GIT_BRANCH);
                    } else if (strcmp(value, "git-commit") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_GIT_COMMIT);
                    } else if (strcmp(value, "script") == 0) {
                        __parser_push_state(s, STATE_RECIPE_SOURCE_SCRIPT);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_RECIPE_SOURCE) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_RECIPE_SOURCE_TYPE, part.source.type, __parse_recipe_part_source_type)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_SCRIPT, part.source.script, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_PATH, part.source.path.path, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_URL, part.source.url.url, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_GIT_REPO, part.source.git.url, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_GIT_BRANCH, part.source.git.branch, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_SOURCE_GIT_COMMIT, part.source.git.commit, __parse_string)

        __consume_sequence_mapped(STATE_RECIPE_STEP_LIST, STATE_RECIPE_STEP)

        case STATE_RECIPE_STEP:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_step(s);
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_TYPE);
                    } else if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_NAME);
                    } else if (strcmp(value, "depends") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_DEPEND_LIST);
                    } else if (strcmp(value, "system") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_SYSTEM);
                    } else if (strcmp(value, "script") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_SCRIPT);
                    } else if (strcmp(value, "meson-cross-file") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_MESON_CROSS_FILE);
                    } else if (strcmp(value, "meson-wraps") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_MESON_WRAPS_LIST);
                    } else if (strcmp(value, "make-in-tree") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_MAKE_INTREE);
                    } else if (strcmp(value, "make-parallel") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_MAKE_PARALLEL);
                    } else if (strcmp(value, "arguments") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_ARGUMENT_LIST);
                    } else if (strcmp(value, "env") == 0) {
                        __parser_push_state(s, STATE_RECIPE_STEP_ENV_LIST_KEY);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_RECIPE_STEP) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_RECIPE_STEP_NAME, step.name, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_STEP_TYPE, step.type, __parse_recipe_step_type)
        __consume_scalar_fn(STATE_RECIPE_STEP_SYSTEM, step.system, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_STEP_SCRIPT, step.script, __parse_string)

        __consume_system_option_scalar_fn(STATE_RECIPE_STEP_MESON_CROSS_FILE, "meson", meson.cross_file, __parse_string)
        __consume_system_option_scalar_fn(STATE_RECIPE_STEP_MAKE_INTREE, "make", make.in_tree, __parse_boolean)
        __consume_system_option_scalar_fn(STATE_RECIPE_STEP_MAKE_PARALLEL, "make", make.parallel, atoi)

        __consume_sequence_unmapped(STATE_RECIPE_STEP_ARGUMENT_LIST, __add_step_arguments)
        __consume_sequence_unmapped(STATE_RECIPE_STEP_DEPEND_LIST, __add_step_depends)

        case STATE_RECIPE_STEP_ENV_LIST_KEY:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    s->env_keypair.key = __parse_string(value);
                    __parser_push_state(s, STATE_RECIPE_STEP_ENV_LIST_VALUE);
                    break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        
        case STATE_RECIPE_STEP_ENV_LIST_VALUE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    s->env_keypair.value = __parse_string(value);

                    __finalize_step_env(s);
                    __parser_pop_state(s);
                    break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_sequence_mapped(STATE_RECIPE_STEP_MESON_WRAPS_LIST, STATE_MESON_WRAP)
        case STATE_MESON_WRAP:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_meson_wrap_item(s);
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_MESON_WRAP_NAME);
                    } else if (strcmp(value, "ingredient") == 0) {
                        __parser_push_state(s, STATE_MESON_WRAP_INGREDIENT);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_MESON_WRAP) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        __consume_scalar_fn(STATE_MESON_WRAP_NAME, meson_wrap_item.name, __parse_string)
        __consume_scalar_fn(STATE_MESON_WRAP_INGREDIENT, meson_wrap_item.ingredient, __parse_string)

        __consume_sequence_mapped(STATE_PACKS_LIST, STATE_PACK)
        case STATE_PACK:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_pack(s);
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_PACK_NAME);
                    } else if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_PACK_TYPE);
                    } else if (strcmp(value, "ingredient-options") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS);
                    } else if (strcmp(value, "filters") == 0) {
                        __parser_push_state(s, STATE_PACK_FILTER_LIST);
                    } else if (strcmp(value, "commands") == 0) {
                        __parser_push_state(s, STATE_PACK_COMMANDS_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PACK) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_PACK_NAME, pack.name, __parse_string)
        __consume_scalar_fn(STATE_PACK_TYPE, pack.type, __parse_pack_type)
        __consume_sequence_unmapped(STATE_PACK_FILTER_LIST, __add_pack_filters)
        __consume_sequence_mapped(STATE_PACK_COMMANDS_LIST, STATE_COMMAND)

        case STATE_PACK_INGREDIENT_OPTIONS:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_pack_ingredient_options(s);
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "bin-paths") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS_BIN_PATHS_LIST);
                    } else if (strcmp(value, "include-paths") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS_INC_PATHS_LIST);
                    } else if (strcmp(value, "lib-paths") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS_LIB_PATHS_LIST);
                    } else if (strcmp(value, "compiler-args") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS_COMPILER_ARGS_LIST);
                    } else if (strcmp(value, "linker-args") == 0) {
                        __parser_push_state(s, STATE_PACK_INGREDIENT_OPTIONS_LINKER_ARGS_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PACK_INGREDIENT_OPTIONS) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        __consume_sequence_unmapped(STATE_PACK_INGREDIENT_OPTIONS_BIN_PATHS_LIST, __add_pack_options_bin_dirs)
        __consume_sequence_unmapped(STATE_PACK_INGREDIENT_OPTIONS_INC_PATHS_LIST, __add_pack_options_inc_dirs)
        __consume_sequence_unmapped(STATE_PACK_INGREDIENT_OPTIONS_LIB_PATHS_LIST, __add_pack_options_lib_dirs)
        __consume_sequence_unmapped(STATE_PACK_INGREDIENT_OPTIONS_COMPILER_ARGS_LIST, __add_pack_options_compiler_flags)
        __consume_sequence_unmapped(STATE_PACK_INGREDIENT_OPTIONS_LINKER_ARGS_LIST, __add_pack_options_linker_flags)

        case STATE_COMMAND:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_command(s);
                    __parser_pop_state(s);
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        __parser_push_state(s, STATE_COMMAND_NAME);
                    } else if (strcmp(value, "description") == 0) {
                        __parser_push_state(s, STATE_COMMAND_DESCRIPTION);
                    } else if (strcmp(value, "path") == 0) {
                        __parser_push_state(s, STATE_COMMAND_PATH);
                    } else if (strcmp(value, "icon") == 0) {
                        __parser_push_state(s, STATE_COMMAND_ICON);
                    } else if (strcmp(value, "system-libs") == 0) {
                        __parser_push_state(s, STATE_COMMAND_SYSTEMLIBS);
                    } else if (strcmp(value, "arguments") == 0) {
                        __parser_push_state(s, STATE_COMMAND_ARGUMENT_LIST);
                    } else if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_COMMAND_TYPE);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_COMMAND) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_COMMAND_NAME, command.name, __parse_string)
        __consume_scalar_fn(STATE_COMMAND_DESCRIPTION, command.description, __parse_string)
        __consume_scalar_fn(STATE_COMMAND_PATH, command.path, __parse_string)
        __consume_scalar_fn(STATE_COMMAND_TYPE, command.type, __parse_command_type)
        __consume_scalar_fn(STATE_COMMAND_ICON, command.icon, __parse_string)
        __consume_sequence_unmapped(STATE_COMMAND_ARGUMENT_LIST, __add_command_arguments)
        
        case STATE_STOP:
            break;
    }
    return 0;
}

int recipe_parse(void* buffer, size_t length, struct recipe** recipeOut)
{
    yaml_parser_t       parser;
    yaml_event_t        event;
    struct parser_state state;
    int                 status;
    
    memset(&state, 0, sizeof(state));
    state.state = STATE_START;

    // initialize some default options
    state.recipe.environment.host.base = 1;
    state.recipe.environment.build.confinement = 1;

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buffer, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            fprintf(stderr, "error: malformed recipe at line %u: %s: %s (code: %i)\n",
                (unsigned int)parser.context_mark.line, parser.context, parser.problem, parser.error);
            return -1;
        }

        status = __consume_event(&state, &event);
        if (status) {
            fprintf(stderr, "error: failed to parse recipe at line %u\n",
                (unsigned int)event.start_mark.line);
            return -1;
        }
        yaml_event_delete(&event);
    } while (state.state != STATE_STOP);

    yaml_parser_delete(&parser);

    // post-process the recipe
    status = recipe_postprocess(&state.recipe);
    if (status) {
        fprintf(stderr, "error: failed to post-process recipe\n");
        return -1;
    }

    // create the recipe and copy all data
    *recipeOut = malloc(sizeof(struct recipe));
    if (!*recipeOut) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    memcpy(*recipeOut, &state.recipe, sizeof(struct recipe));
    return 0;
}

#define __destroy_list(fn, list, type) \
    do { \
        struct list_item* item; \
        while ((item = (list))) { \
            (list) = item->next; \
            __destroy_##fn((type*)item); \
        } \
    } while (0)

static void __destroy_string(struct list_item_string* value)
{
    free((void*)value->value);
    free(value);
}

static void __destroy_keypair(struct chef_keypair_item* keypair)
{
    free((void*)keypair->key);
    free((void*)keypair->value);
    free(keypair);
}

static void __destroy_project(struct recipe_project* project)
{
    free((void*)project->summary);
    free((void*)project->description);
    free((void*)project->version);
    free((void*)project->url);
    free((void*)project->license);
    free((void*)project->author);
    free((void*)project->email);
    // do not free project itself, part of recipe
}

static void __destroy_ingredient(struct recipe_ingredient* ingredient)
{
    free((void*)ingredient->name);
    free((void*)ingredient->version);
    free((void*)ingredient->channel);
    free(ingredient);
}

static void __destroy_step(struct recipe_step* step)
{
    __destroy_list(string, step->depends.head, struct list_item_string);
    __destroy_list(string, step->arguments.head, struct list_item_string);
    __destroy_list(keypair, step->env_keypairs.head, struct chef_keypair_item);
    free((void*)step->system);
    free(step);
}

static void __destroy_part(struct recipe_part* part)
{
    __destroy_list(step, part->steps.head, struct recipe_step);

    if (part->source.type == RECIPE_PART_SOURCE_TYPE_PATH) {
        free((void*)part->source.path.path);
    } else if (part->source.type == RECIPE_PART_SOURCE_TYPE_URL) {
        free((void*)part->source.url.url);
    } else if (part->source.type == RECIPE_PART_SOURCE_TYPE_GIT) {
        free((void*)part->source.git.url);
        free((void*)part->source.git.branch);
        free((void*)part->source.git.commit);
    }

    free((void*)part->name);
    free(part);
}

static void __destroy_command(struct recipe_pack_command* command)
{
    __destroy_list(string, command->arguments.head, struct list_item_string);
    free((void*)command->name);
    free((void*)command->description);
    free((void*)command->path);
    free(command);
}

static void __destroy_pack_ingredient_options(struct recipe_pack_ingredient_options* options)
{
    __destroy_list(string, options->bin_dirs.head, struct list_item_string);
    __destroy_list(string, options->inc_dirs.head, struct list_item_string);
    __destroy_list(string, options->lib_dirs.head, struct list_item_string);
    __destroy_list(string, options->compiler_flags.head, struct list_item_string);
    __destroy_list(string, options->linker_flags.head, struct list_item_string);
}

static void __destroy_pack(struct recipe_pack* pack)
{
    __destroy_pack_ingredient_options(&pack->options);
    __destroy_list(command, pack->commands.head, struct recipe_pack_command);
    __destroy_list(string, pack->filters.head, struct list_item_string);
    free((void*)pack->name);
    free(pack);
}

void recipe_destroy(struct recipe* recipe)
{
    if (!recipe) {
        return;
    }

    __destroy_project(&recipe->project);
    __destroy_list(ingredient, recipe->environment.host.ingredients.head, struct recipe_ingredient);
    __destroy_list(ingredient, recipe->environment.build.ingredients.head, struct recipe_ingredient);
    __destroy_list(ingredient, recipe->environment.runtime.ingredients.head, struct recipe_ingredient);
    __destroy_list(part, recipe->parts.head, struct recipe_part);
    __destroy_list(pack, recipe->packs.head, struct recipe_pack);
    free(recipe);
}
