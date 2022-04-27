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

#include <ctype.h>
#include <libplatform.h>
#include <recipe.h>
#include <yaml/yaml.h>
#include <stdio.h>
#include <stdlib.h>

enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_PROJECT,
    STATE_PROJECT_SUMMARY,
    STATE_PROJECT_DESCRIPTION,
    STATE_PROJECT_ICON,
    STATE_PROJECT_AUTHOR,
    STATE_PROJECT_EMAIL,
    STATE_PROJECT_VERSION,
    STATE_PROJECT_LICENSE,
    STATE_PROJECT_EULA,
    STATE_PROJECT_HOMEPAGE,

    STATE_INGREDIENT_LIST,

    STATE_INGREDIENT,       // MAPPING_START
    STATE_INGREDIENT_NAME,
    STATE_INGREDIENT_VERSION,
    STATE_INGREDIENT_INCLUDE,
    STATE_INGREDIENT_INCLUDE_FILTERS_LIST,
    STATE_INGREDIENT_DESCRIPTION,
    STATE_INGREDIENT_PLATFORM,
    STATE_INGREDIENT_ARCH,
    STATE_INGREDIENT_CHANNEL,
    STATE_INGREDIENT_SOURCE,

    STATE_INGREDIENT_SOURCE_TYPE,
    STATE_INGREDIENT_SOURCE_URL,
    STATE_INGREDIENT_SOURCE_CHANNEL,

    STATE_RECIPE_LIST,
    STATE_RECIPE,          // MAPPING_START
    STATE_RECIPE_NAME,
    STATE_RECIPE_PATH,
    STATE_RECIPE_TOOLCHAIN,

    STATE_RECIPE_STEP_LIST,
    STATE_RECIPE_STEP,     // MAPPING_START
    STATE_RECIPE_STEP_NAME,
    STATE_RECIPE_STEP_TYPE,
    STATE_RECIPE_STEP_DEPEND_LIST,
    STATE_RECIPE_STEP_SYSTEM,
    STATE_RECIPE_STEP_SCRIPT,
    STATE_RECIPE_STEP_ARGUMENT_LIST,

    STATE_RECIPE_STEP_MESON_CROSS_FILE,

    STATE_RECIPE_STEP_MAKE_INTREE,
    STATE_RECIPE_STEP_MAKE_PARALLEL,

    STATE_RECIPE_STEP_ENV_LIST_KEY,
    STATE_RECIPE_STEP_ENV_LIST_VALUE,

    STATE_PACKS_LIST,
    STATE_PACK,            // MAPPING_START
    STATE_PACK_NAME,
    STATE_PACK_TYPE,
    STATE_PACK_FILTER_LIST,
    STATE_PACK_COMMANDS_LIST,

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
    enum state               state;
    struct recipe            recipe;
    struct recipe_ingredient ingredient;
    struct recipe_part       part;
    struct recipe_step       step;
    struct recipe_pack       pack;
    struct oven_pack_command command;
    struct oven_keypair_item env_keypair;
};

static const char* __parse_string(const char* value)
{
    if (value == NULL || strlen(value) == 0) {
        return NULL;
    }

    return strdup(value);
}

static enum chef_package_type __parse_pack_type(const char* value)
{
    if (strcmp(value, "ingredient") == 0) {
        return CHEF_PACKAGE_TYPE_INGREDIENT;
    } else if (strcmp(value, "application") == 0) {
        return CHEF_PACKAGE_TYPE_APPLICATION;
    } else if (strcmp(value, "toolchain") == 0) {
        return CHEF_PACKAGE_TYPE_TOOLCHAIN;
    } else {
        return CHEF_PACKAGE_TYPE_UNKNOWN;
    }
}

static enum ingredient_source __parse_ingredient_source_type(const char* value)
{
    if (value == NULL || strlen(value) == 0) {
        return INGREDIENT_SOURCE_REPO;
    }

    if (strcmp(value, "repo") == 0) {
        return INGREDIENT_SOURCE_REPO;
    } else if (strcmp(value, "url") == 0) {
        return INGREDIENT_SOURCE_URL;
    } else if (strcmp(value, "local") == 0) {
        return INGREDIENT_SOURCE_FILE;
    } else {
        return INGREDIENT_SOURCE_UNKNOWN;
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

static void __finalize_project(struct parser_state* state)
{
    // verify required project members
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

static void __finalize_ingredient(struct parser_state* state)
{
    struct recipe_ingredient* ingredient;

    // we should verify required members of the ingredient before creating a copy
    if (state->ingredient.ingredient.name == NULL) {
        fprintf(stderr, "parse error: ingredient name is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->ingredient.ingredient.channel == NULL) {
        fprintf(stderr, "parse error: ingredient %s: channel is required\n", state->ingredient.ingredient.name);
        exit(EXIT_FAILURE);
    }

    switch (state->ingredient.ingredient.source) {
        case INGREDIENT_SOURCE_URL:
            if (state->ingredient.ingredient.url.url == NULL) {
                fprintf(stderr, "parse error: ingredient %s: url is required\n", state->ingredient.ingredient.name);
                exit(EXIT_FAILURE);
            }
            break;
        case INGREDIENT_SOURCE_FILE:
            if (state->ingredient.ingredient.file.path == NULL) {
                fprintf(stderr, "parse error: ingredient %s: file path is required\n", state->ingredient.ingredient.name);
                exit(EXIT_FAILURE);
            }
            break;
        case INGREDIENT_SOURCE_UNKNOWN:
            fprintf(stderr, "parse error: ingredient %s: type is not supported\n", state->ingredient.ingredient.name);
            exit(EXIT_FAILURE);
            break;
            
        default:
            break;
    }

    // handle "host" values in arch and platform
    if (state->ingredient.ingredient.arch != NULL &&
        strcmp(state->ingredient.ingredient.arch, "host") == 0) {
        free((void*)state->ingredient.ingredient.arch);
        state->ingredient.ingredient.arch = strdup(CHEF_ARCHITECTURE_STR);
    }

    if (state->ingredient.ingredient.platform != NULL &&
        strcmp(state->ingredient.ingredient.platform, "host") == 0) {
        free((void*)state->ingredient.ingredient.platform);
        state->ingredient.ingredient.platform = strdup(CHEF_PLATFORM_STR);
    }

    // now we copy and reset
    ingredient = malloc(sizeof(struct recipe_ingredient));
    if (ingredient == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    // copy the set values
    memcpy(ingredient, &state->ingredient, sizeof(struct recipe_ingredient));
    list_add(&state->recipe.ingredients, &ingredient->list_header);

    // reset the structure in state
    memset(&state->ingredient, 0, sizeof(struct recipe_ingredient));
    state->ingredient.ingredient.source = INGREDIENT_SOURCE_REPO;
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

static void __finalize_part(struct parser_state* state)
{
    struct recipe_part* part;

    // we should verify required members of the part before creating a copy
    if (__is_valid_name(state->part.name)) {
        fprintf(stderr, "parse error: part name must be provided and only contain [a-zA-Z_-]\n");
        exit(EXIT_FAILURE);
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
        struct oven_value_item* value = (struct oven_value_item*)item;
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
    struct oven_keypair_item* keypair;

    // key value must be provided
    if (state->env_keypair.key == NULL || strlen(state->env_keypair.key) == 0) {
        return;
    }

    keypair = malloc(sizeof(struct oven_keypair_item));
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
    struct oven_pack_command* command;

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
    command = malloc(sizeof(struct oven_pack_command));
    if (command == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    memcpy(command, &state->command, sizeof(struct oven_pack_command));
    list_add(&state->pack.commands, &command->list_header);

    // reset the structure in state
    memset(&state->command, 0, sizeof(struct oven_pack_command));
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

// TODO error handling
#define DEFINE_LIST_STRING_ADD(structure, field) \
    static void __add_##structure ##_##field(struct parser_state* state, const char* value) \
    { \
        struct oven_value_item* argument; \
        if (value == NULL || strlen(value) == 0) { \
            return; \
        } \
        \
        argument = malloc(sizeof(struct oven_value_item)); \
        if (!argument) { \
            fprintf(stderr, "error: out of memory\n"); \
            exit(EXIT_FAILURE); \
        } \
        \
        argument->value = strdup(value); \
        \
        list_add(&state->structure.field, &argument->list_header); \
    }

DEFINE_LIST_STRING_ADD(ingredient, filters)
DEFINE_LIST_STRING_ADD(step, depends)
DEFINE_LIST_STRING_ADD(step, arguments)
DEFINE_LIST_STRING_ADD(pack, filters)
DEFINE_LIST_STRING_ADD(command, arguments)

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
                    s->state = STATE_STREAM;
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_STREAM:
            switch (event->type) {
                case YAML_DOCUMENT_START_EVENT:
                    s->state = STATE_DOCUMENT;
                    break;
                case YAML_STREAM_END_EVENT:
                    s->state = STATE_STOP;
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DOCUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->state = STATE_SECTION;
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    s->state = STATE_STREAM;
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
                        s->state = STATE_PROJECT;
                    }
                    else if (strcmp(value, "ingredients") == 0) {
                        s->state = STATE_INGREDIENT_LIST;
                    }
                    else if (strcmp(value, "recipes") == 0) {
                        s->state = STATE_RECIPE_LIST;
                    }
                    else if (strcmp(value, "packs") == 0) {
                        s->state = STATE_PACKS_LIST;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                
                case YAML_MAPPING_END_EVENT:
                    __finalize_recipe(s);
                    s->state = STATE_DOCUMENT;
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
                    s->state = STATE_SECTION;
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "summary") == 0) {
                        s->state = STATE_PROJECT_SUMMARY;
                    }
                    else if (strcmp(value, "description") == 0) {
                        s->state = STATE_PROJECT_DESCRIPTION;
                    }
                    else if (strcmp(value, "icon") == 0) {
                        s->state = STATE_PROJECT_ICON;
                    }
                    else if (strcmp(value, "author") == 0) {
                        s->state = STATE_PROJECT_AUTHOR;
                    }
                    else if (strcmp(value, "email") == 0) {
                        s->state = STATE_PROJECT_EMAIL;
                    }
                    else if (strcmp(value, "version") == 0) {
                        s->state = STATE_PROJECT_VERSION;
                    }
                    else if (strcmp(value, "license") == 0) {
                        s->state = STATE_PROJECT_LICENSE;
                    }
                    else if (strcmp(value, "eula") == 0) {
                        s->state = STATE_PROJECT_EULA;
                    }
                    else if (strcmp(value, "homepage") == 0) {
                        s->state = STATE_PROJECT_HOMEPAGE;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

#define __consume_scalar_fn(__INITSTATE, __STATE, __FIELD, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SCALAR_EVENT: \
                    value = (char *)event->data.scalar.value; \
                    s->__FIELD = __FN(value); \
                    s->state = __INITSTATE; \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_system_option_scalar_fn(__INITSTATE, __STATE, __SYSTEM, __FIELD, __FN) \
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
                    s->state = __INITSTATE; \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_sequence_mapped(__INITSTATE, __LISTSTATE, __ITEMSTATE) \
        case __LISTSTATE: \
            switch (event->type) { \
                case YAML_SEQUENCE_START_EVENT: \
                    break; \
                case YAML_SEQUENCE_END_EVENT: \
                    s->state = __INITSTATE; \
                    break; \
                case YAML_MAPPING_START_EVENT: \
                    s->state = __ITEMSTATE; \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_sequence_unmapped(__INITSTATE, __STATE, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SEQUENCE_START_EVENT: \
                    break; \
                case YAML_SEQUENCE_END_EVENT: \
                    s->state = __INITSTATE; \
                    break; \
                case YAML_SCALAR_EVENT: \
                    __FN(s, (char *)event->data.scalar.value); \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;


        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_SUMMARY, recipe.project.summary, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_DESCRIPTION, recipe.project.description, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_ICON, recipe.project.icon, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_AUTHOR, recipe.project.author, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_EMAIL, recipe.project.email, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_VERSION, recipe.project.version, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_LICENSE, recipe.project.license, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_EULA, recipe.project.eula, __parse_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_HOMEPAGE, recipe.project.url, __parse_string)

        __consume_sequence_mapped(STATE_SECTION, STATE_INGREDIENT_LIST, STATE_INGREDIENT)

        case STATE_INGREDIENT:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_INGREDIENT_NAME;
                    }
                    else if (strcmp(value, "description") == 0) {
                        s->state = STATE_INGREDIENT_DESCRIPTION;
                    }
                    else if (strcmp(value, "platform") == 0) {
                        s->state = STATE_INGREDIENT_PLATFORM;
                    }
                    else if (strcmp(value, "arch") == 0) {
                        s->state = STATE_INGREDIENT_ARCH;
                    }
                    else if (strcmp(value, "channel") == 0) {
                        s->state = STATE_INGREDIENT_CHANNEL;
                    }
                    else if (strcmp(value, "version") == 0) {
                        s->state = STATE_INGREDIENT_VERSION;
                    }
                    else if (strcmp(value, "include-filters") == 0) {
                        s->state = STATE_INGREDIENT_INCLUDE_FILTERS_LIST;
                    }
                    else if (strcmp(value, "include") == 0) {
                        s->state = STATE_INGREDIENT_INCLUDE;
                    }
                    else if (strcmp(value, "source") == 0) {
                        s->state = STATE_INGREDIENT_SOURCE;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_ingredient(s);
                    s->state = STATE_INGREDIENT_LIST;
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_NAME, ingredient.ingredient.name, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_DESCRIPTION, ingredient.ingredient.description, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_PLATFORM, ingredient.ingredient.platform, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_ARCH, ingredient.ingredient.arch, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_CHANNEL, ingredient.ingredient.channel, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_VERSION, ingredient.ingredient.version, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_INCLUDE, ingredient.include, __parse_boolean)
        __consume_sequence_unmapped(STATE_INGREDIENT, STATE_INGREDIENT_INCLUDE_FILTERS_LIST, __add_ingredient_filters)

        case STATE_INGREDIENT_SOURCE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_INGREDIENT;
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        s->state = STATE_INGREDIENT_SOURCE_TYPE;
                    }
                    else if (strcmp(value, "url") == 0) {
                        s->state = STATE_INGREDIENT_SOURCE_URL;
                    }
                    else if (strcmp(value, "channel") == 0) {
                        s->state = STATE_INGREDIENT_SOURCE_CHANNEL;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_INGREDIENT_SOURCE, STATE_INGREDIENT_SOURCE_TYPE, ingredient.ingredient.source, __parse_ingredient_source_type)
        __consume_scalar_fn(STATE_INGREDIENT_SOURCE, STATE_INGREDIENT_SOURCE_CHANNEL, ingredient.ingredient.repo.channel, __parse_string)
        __consume_scalar_fn(STATE_INGREDIENT_SOURCE, STATE_INGREDIENT_SOURCE_URL, ingredient.ingredient.url.url, __parse_string)

        __consume_sequence_mapped(STATE_SECTION, STATE_RECIPE_LIST, STATE_RECIPE)

        case STATE_RECIPE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_part(s);
                    s->state = STATE_RECIPE_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_RECIPE_NAME;
                    }
                    else if (strcmp(value, "path") == 0) {
                        s->state = STATE_RECIPE_PATH;
                    }
                    else if (strcmp(value, "toolchain") == 0) {
                        s->state = STATE_RECIPE_TOOLCHAIN;
                    }
                    else if (strcmp(value, "steps") == 0) {
                        s->state = STATE_RECIPE_STEP_LIST;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_RECIPE, STATE_RECIPE_NAME, part.name, __parse_string)
        __consume_scalar_fn(STATE_RECIPE, STATE_RECIPE_PATH, part.path, __parse_string)
        __consume_scalar_fn(STATE_RECIPE, STATE_RECIPE_TOOLCHAIN, part.toolchain, __parse_string)

        __consume_sequence_mapped(STATE_RECIPE, STATE_RECIPE_STEP_LIST, STATE_RECIPE_STEP)

        case STATE_RECIPE_STEP:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_step(s);
                    s->state = STATE_RECIPE_STEP_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        s->state = STATE_RECIPE_STEP_TYPE;
                    } else if (strcmp(value, "name") == 0) {
                        s->state = STATE_RECIPE_STEP_NAME;
                    } else if (strcmp(value, "depends") == 0) {
                        s->state = STATE_RECIPE_STEP_DEPEND_LIST;
                    } else if (strcmp(value, "system") == 0) {
                        s->state = STATE_RECIPE_STEP_SYSTEM;
                    } else if (strcmp(value, "script") == 0) {
                        s->state = STATE_RECIPE_STEP_SCRIPT;
                    } else if (strcmp(value, "meson-cross-file") == 0) {
                        s->state = STATE_RECIPE_STEP_MESON_CROSS_FILE;
                    } else if (strcmp(value, "make-in-tree") == 0) {
                        s->state = STATE_RECIPE_STEP_MAKE_INTREE;
                    } else if (strcmp(value, "make-parallel") == 0) {
                        s->state = STATE_RECIPE_STEP_MAKE_PARALLEL;
                    } else if (strcmp(value, "arguments") == 0) {
                        s->state = STATE_RECIPE_STEP_ARGUMENT_LIST;
                    } else if (strcmp(value, "env") == 0) {
                        s->state = STATE_RECIPE_STEP_ENV_LIST_KEY;
                    } else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_NAME, step.name, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_TYPE, step.type, __parse_recipe_step_type)
        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_SYSTEM, step.system, __parse_string)
        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_SCRIPT, step.script, __parse_string)

        __consume_system_option_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_MESON_CROSS_FILE, "meson", meson.cross_file, __parse_string)
        __consume_system_option_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_MAKE_INTREE, "make", make.in_tree, __parse_boolean)
        __consume_system_option_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_MAKE_PARALLEL, "make", make.parallel, atoi)

        __consume_sequence_unmapped(STATE_RECIPE_STEP, STATE_RECIPE_STEP_ARGUMENT_LIST, __add_step_arguments)
        __consume_sequence_unmapped(STATE_RECIPE_STEP, STATE_RECIPE_STEP_DEPEND_LIST, __add_step_depends)

        case STATE_RECIPE_STEP_ENV_LIST_KEY:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_RECIPE_STEP;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    s->env_keypair.key = __parse_string(value);
                    s->state = STATE_RECIPE_STEP_ENV_LIST_VALUE;
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
                    s->state = STATE_RECIPE_STEP_ENV_LIST_KEY;
                    break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_sequence_mapped(STATE_SECTION, STATE_PACKS_LIST, STATE_PACK)
        case STATE_PACK:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_pack(s);
                    s->state = STATE_PACKS_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_PACK_NAME;
                    }
                    else if (strcmp(value, "type") == 0) {
                        s->state = STATE_PACK_TYPE;
                    }
                    else if (strcmp(value, "filters") == 0) {
                        s->state = STATE_PACK_FILTER_LIST;
                    }
                    else if (strcmp(value, "commands") == 0) {
                        s->state = STATE_PACK_COMMANDS_LIST;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_PACK, STATE_PACK_NAME, pack.name, __parse_string)
        __consume_scalar_fn(STATE_PACK, STATE_PACK_TYPE, pack.type, __parse_pack_type)
        __consume_sequence_unmapped(STATE_PACK, STATE_PACK_FILTER_LIST, __add_pack_filters)
        __consume_sequence_mapped(STATE_PACK, STATE_PACK_COMMANDS_LIST, STATE_COMMAND)

        case STATE_COMMAND:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_command(s);
                    s->state = STATE_PACK_COMMANDS_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_COMMAND_NAME;
                    } else if (strcmp(value, "description") == 0) {
                        s->state = STATE_COMMAND_DESCRIPTION;
                    } else if (strcmp(value, "path") == 0) {
                        s->state = STATE_COMMAND_PATH;
                    } else if (strcmp(value, "icon") == 0) {
                        s->state = STATE_COMMAND_ICON;
                    } else if (strcmp(value, "system-libs") == 0) {
                        s->state = STATE_COMMAND_SYSTEMLIBS;
                    } else if (strcmp(value, "arguments") == 0) {
                        s->state = STATE_COMMAND_ARGUMENT_LIST;
                    } else if (strcmp(value, "type") == 0) {
                        s->state = STATE_COMMAND_TYPE;
                    } else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_NAME, command.name, __parse_string)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_DESCRIPTION, command.description, __parse_string)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_PATH, command.path, __parse_string)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_TYPE, command.type, __parse_command_type)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_ICON, command.icon, __parse_string)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_SYSTEMLIBS, command.allow_system_libraries, __parse_boolean)
        __consume_sequence_unmapped(STATE_COMMAND, STATE_COMMAND_ARGUMENT_LIST, __add_command_arguments)
        
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
    state.ingredient.ingredient.source = INGREDIENT_SOURCE_REPO;

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buffer, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            fprintf(stderr, "error: malformed recipe at line %u: %s: %s (code: %i)\n",
                (unsigned int)parser.context_mark.line, parser.context, parser.problem, parser.error);
            return -1;
        }

        status = __consume_event(&state, &event );
        if (status) {
            fprintf(stderr, "error: failed to parse recipe at line %u\n",
                (unsigned int)event.start_mark.line);
            return -1;
        }
        yaml_event_delete(&event);
    } while (state.state != STATE_STOP);

    yaml_parser_delete(&parser);

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

static void __destroy_string(struct oven_value_item* value)
{
    free((void*)value->value);
    free(value);
}

static void __destroy_keypair(struct oven_keypair_item* keypair)
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
    free((void*)ingredient->ingredient.name);
    free((void*)ingredient->ingredient.version);
    free((void*)ingredient->ingredient.platform);
    free((void*)ingredient->ingredient.arch);
    free((void*)ingredient->ingredient.channel);
    free((void*)ingredient->ingredient.description);

    if (ingredient->ingredient.source == INGREDIENT_SOURCE_URL) {
        free((void*)ingredient->ingredient.url.url);
    }
    else if (ingredient->ingredient.source == INGREDIENT_SOURCE_FILE) {
        free((void*)ingredient->ingredient.file.path);
    }
    free(ingredient);
}

static void __destroy_step(struct recipe_step* step)
{
    __destroy_list(string, step->depends.head, struct oven_value_item);
    __destroy_list(string, step->arguments.head, struct oven_value_item);
    __destroy_list(keypair, step->env_keypairs.head, struct oven_keypair_item);
    free((void*)step->system);
    free(step);
}

static void __destroy_part(struct recipe_part* part)
{
    __destroy_list(step, part->steps.head, struct recipe_step);
    free((void*)part->name);
    free((void*)part->path);
    free(part);
}

static void __destroy_command(struct oven_pack_command* command)
{
    __destroy_list(string, command->arguments.head, struct oven_value_item);
    free((void*)command->name);
    free((void*)command->description);
    free((void*)command->path);
    free(command);
}

static void __destroy_pack(struct recipe_pack* pack)
{
    __destroy_list(command, pack->commands.head, struct oven_pack_command);
    __destroy_list(string, pack->filters.head, struct oven_value_item);
    free((void*)pack->name);
    free(pack);
}

void recipe_destroy(struct recipe* recipe)
{
    if (!recipe) {
        return;
    }

    __destroy_project(&recipe->project);
    __destroy_list(ingredient, recipe->ingredients.head, struct recipe_ingredient);
    __destroy_list(part, recipe->parts.head, struct recipe_part);
    __destroy_list(pack, recipe->packs.head, struct recipe_pack);
    free(recipe);
}
