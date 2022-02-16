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

#include <errno.h>
#include <recipe.h>
#include <yaml/yaml.h>
#include <stdio.h>
#include <stdlib.h>

/*
stream-start-event (1)
  document-start-event (3)
    mapping-start-event (9)
      scalar-event (6) = {value="project", length=7}
      mapping-start-event (9)
        scalar-event (6) = {value="name", length=4}
        scalar-event (6) = {value="Simple Application Recipe", length=25}
        scalar-event (6) = {value="description", length=11}
        scalar-event (6) = {value="A simple application recipe", length=27}
        scalar-event (6) = {value="type", length=4}
        scalar-event (6) = {value="application", length=11}
        scalar-event (6) = {value="version", length=7}
        scalar-event (6) = {value="0.1", length=3}
        scalar-event (6) = {value="license", length=7}
        scalar-event (6) = {value="MIT", length=3}
        scalar-event (6) = {value="homepage", length=8}
        scalar-event (6) = {value="", length=0}
      mapping-end-event (10)
      scalar-event (6) = {value="ingredients", length=11}
      sequence-start-event (7)
        mapping-start-event (9)
          scalar-event (6) = {value="name", length=4}
          scalar-event (6) = {value="libc", length=4}
          scalar-event (6) = {value="version", length=7}
          scalar-event (6) = {value="0.2", length=3}
          scalar-event (6) = {value="description", length=11}
          scalar-event (6) = {value="A library", length=9}
          scalar-event (6) = {value="source", length=6}
          mapping-start-event (9)
            scalar-event (6) = {value="type", length=4}
            scalar-event (6) = {value="archive", length=7}
            scalar-event (6) = {value="url", length=3}
            scalar-event (6) = {value="", length=0}
          mapping-end-event (10)
        mapping-end-event (10)
      sequence-end-event (8)
      scalar-event (6) = {value="recipes", length=7}
      mapping-start-event (9)
        scalar-event (6) = {value="recipe", length=6}
        sequence-start-event (7)
          mapping-start-event (9)
            scalar-event (6) = {value="name", length=4}
            scalar-event (6) = {value="my-app", length=6}
            scalar-event (6) = {value="path", length=4}
            scalar-event (6) = {value="source/", length=7}
            scalar-event (6) = {value="steps", length=5}
            sequence-start-event (7)
              mapping-start-event (9)
                scalar-event (6) = {value="type", length=4}
                scalar-event (6) = {value="generate", length=8}
                scalar-event (6) = {value="system", length=6}
                scalar-event (6) = {value="configure", length=9}
                scalar-event (6) = {value="arguments", length=9}
                scalar-event (6) = {value="--platform=amd64", length=16}
                scalar-event (6) = {value="env", length=3}
                mapping-start-event (9)
                  scalar-event (6) = {value="VAR", length=3}
                  scalar-event (6) = {value="VALUE", length=5}
                mapping-end-event (10)
              mapping-end-event (10)
              mapping-start-event (9)
                scalar-event (6) = {value="type", length=4}
                scalar-event (6) = {value="build", length=5}
                scalar-event (6) = {value="depends", length=7}
                scalar-event (6) = {value="generate", length=8}
                scalar-event (6) = {value="system", length=6}
                scalar-event (6) = {value="make", length=4}
                scalar-event (6) = {value="arguments", length=9}
                scalar-event (6) = {value="mytarget", length=8}
                scalar-event (6) = {value="env", length=3}
                mapping-start-event (9)
                  scalar-event (6) = {value="VAR", length=3}
                  scalar-event (6) = {value="VALUE", length=5}
                mapping-end-event (10)
              mapping-end-event (10)
            sequence-end-event (8)
          mapping-end-event (10)
        sequence-end-event (8)
      mapping-end-event (10)
      scalar-event (6) = {value="commands", length=8}
      mapping-start-event (9)
        scalar-event (6) = {value="myapp", length=5}
        mapping-start-event (9)
          scalar-event (6) = {value="path", length=4}
          scalar-event (6) = {value="/bin/myapp", length=10}
          scalar-event (6) = {value="arguments", length=9}
          sequence-start-event (7)
            scalar-event (6) = {value="--arg1 --arg2", length=13}
          sequence-end-event (8)
          scalar-event (6) = {value="type", length=4}
          scalar-event (6) = {value="executable", length=10}
          scalar-event (6) = {value="description", length=11}
          scalar-event (6) = {value="A simple application", length=20}
        mapping-end-event (10)
      mapping-end-event (10)
    mapping-end-event (10)
  document-end-event (4)
stream-end-event (2)
*/

enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_PROJECT,
    STATE_PROJECT_NAME,
    STATE_PROJECT_DESCRIPTION,
    STATE_PROJECT_AUTHOR,
    STATE_PROJECT_EMAIL,
    STATE_PROJECT_TYPE,
    STATE_PROJECT_VERSION,
    STATE_PROJECT_LICENSE,
    STATE_PROJECT_HOMEPAGE,

    STATE_INGREDIENT_LIST,

    STATE_INGREDIENT,       // MAPPING_START
    STATE_INGREDIENT_NAME,
    STATE_INGREDIENT_VERSION,
    STATE_INGREDIENT_DESCRIPTION,
    STATE_INGREDIENT_SOURCE,

    STATE_INGREDIENT_SOURCE_TYPE,
    STATE_INGREDIENT_SOURCE_URL,

    STATE_RECIPE_LIST,

    STATE_RECIPE,          // MAPPING_START
    STATE_RECIPE_NAME,
    STATE_RECIPE_PATH,

    STATE_RECIPE_STEP_LIST,

    STATE_RECIPE_STEP,     // MAPPING_START
    STATE_RECIPE_STEP_TYPE,
    STATE_RECIPE_STEP_DEPEND_LIST,
    STATE_RECIPE_STEP_SYSTEM,
    STATE_RECIPE_STEP_ARGUMENT_LIST,
    STATE_RECIPE_STEP_ENV_LIST,

    STATE_RECIPE_STEP_DEPENDENCY,
    STATE_RECIPE_STEP_ARGUMENT,
    STATE_RECIPE_STEP_ENV,

    STATE_COMMANDS_LIST,

    STATE_COMMAND,         // MAPPING_START
    STATE_COMMAND_NAME,
    STATE_COMMAND_PATH,
    STATE_COMMAND_ARGUMENT_LIST,
    STATE_COMMAND_TYPE,
    STATE_COMMAND_DESCRIPTION,

    STATE_COMMAND_ARGUMENT,

    STATE_STOP
};

struct parser_state {
    enum state    state;
    struct recipe recipe;
};

static int __parse_boolean(const char* string, int* value)
{
    char*  t[] = {"y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON", NULL};
    char*  f[] = {"n", "N", "no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF", NULL};
    char** p;

    for (p = t; *p; p++) {
        if (strcmp(string, *p) == 0) {
            *value = 1;
            return 0;
        }
    }
    for (p = f; *p; p++) {
        if (strcmp(string, *p) == 0) {
            *value = 0;
            return 0;
        }
    }
    return EINVAL;
}

static const char* __parse_project_string(const char* value)
{

}

static enum recipe_type __parse_project_type(const char* value)
{

}

static int __consume_event(struct parser_state* s, yaml_event_t* event)
{
    char *value;
    printf("__consume_event(state=%d event=%d)\n", s->state, event->type);

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
                    else if (strcmp(value, "commands") == 0) {
                        s->state = STATE_COMMANDS_LIST;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_DOCUMENT_END_EVENT:
                    s->state = STATE_STREAM;
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
                    s->state = STATE_SECTION;
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_PROJECT_NAME;
                    }
                    else if (strcmp(value, "description") == 0) {
                        s->state = STATE_PROJECT_DESCRIPTION;
                    }
                    else if (strcmp(value, "type") == 0) {
                        s->state = STATE_PROJECT_TYPE;
                    }
                    else if (strcmp(value, "version") == 0) {
                        s->state = STATE_PROJECT_VERSION;
                    }
                    else if (strcmp(value, "license") == 0) {
                        s->state = STATE_PROJECT_LICENSE;
                    }
                    else if (strcmp(value, "homepage") == 0) {
                        s->state = STATE_PROJECT_HOMEPAGE;
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

#define __consume_scalar_fn(__INITSTATE, __STATE, __FIELD, __FN) \
        case __STATE: \
            switch (event->type) { \
                case YAML_SCALAR_EVENT: \
                    value = (char *)event->data.scalar.value; \
                    s->recipe.__FIELD = __FN(value); \
                    s->state = __INITSTATE; \
                    break; \
                default: \
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state); \
                    return -1; \
            } \
            break;

#define __consume_sequence(__INITSTATE, __LISTSTATE, __ITEMSTATE) \
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

        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_NAME, name, __parse_project_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_DESCRIPTION, description, __parse_project_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_TYPE, type, __parse_project_type)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_VERSION, version, __parse_project_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_LICENSE, license, __parse_project_string)
        __consume_scalar_fn(STATE_PROJECT, STATE_PROJECT_HOMEPAGE, url, __parse_project_string)

        __consume_sequence(STATE_SECTION, STATE_INGREDIENT_LIST, STATE_INGREDIENT)

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
                    else if (strcmp(value, "version") == 0) {
                        s->state = STATE_INGREDIENT_VERSION;
                    }
                    else if (strcmp(value, "source") == 0) {
                        s->state = STATE_INGREDIENT_SOURCE;
                    }
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_INGREDIENT_LIST;
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_NAME, name, __parse_ingredient_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_DESCRIPTION, description, __parse_ingredient_string)
        __consume_scalar_fn(STATE_INGREDIENT, STATE_INGREDIENT_VERSION, version, __parse_ingredient_string)

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
                    else {
                        fprintf(stderr, "__consume_event: unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_INGREDIENT_SOURCE, STATE_INGREDIENT_SOURCE_TYPE, type, __parse_ingredient_source_type)
        __consume_scalar_fn(STATE_INGREDIENT_SOURCE, STATE_INGREDIENT_SOURCE_URL, url, __parse_ingredient_source_string)

        __consume_sequence(STATE_SECTION, STATE_RECIPE_LIST, STATE_RECIPE)

        case STATE_RECIPE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
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

        __consume_scalar_fn(STATE_RECIPE, STATE_RECIPE_NAME, name, __parse_recipe_string)
        __consume_scalar_fn(STATE_RECIPE, STATE_RECIPE_PATH, path, __parse_recipe_string)

        __consume_sequence(STATE_RECIPE, STATE_RECIPE_STEP_LIST, STATE_RECIPE_STEP)

        case STATE_RECIPE_STEP:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_RECIPE_STEP_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        s->state = STATE_RECIPE_STEP_TYPE;
                    }
                    else if (strcmp(value, "depends") == 0) {
                        s->state = STATE_RECIPE_STEP_DEPEND_LIST;
                    }
                    else if (strcmp(value, "system") == 0) {
                        s->state = STATE_RECIPE_STEP_SYSTEM;
                    }
                    else if (strcmp(value, "arguments") == 0) {
                        s->state = STATE_RECIPE_STEP_ARGUMENT_LIST;
                    }
                    else if (strcmp(value, "env") == 0) {
                        s->state = STATE_RECIPE_STEP_ENV_LIST;
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

        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_TYPE, type, __parse_recipe_step_type)
        __consume_sequence(STATE_RECIPE_STEP, STATE_RECIPE_STEP_DEPEND_LIST, STATE_RECIPE_STEP_DEPENDENCY)
        __consume_scalar_fn(STATE_RECIPE_STEP, STATE_RECIPE_STEP_SYSTEM, system, __parse_recipe_step_string)
        __consume_sequence(STATE_RECIPE_STEP, STATE_RECIPE_STEP_ARGUMENT_LIST, STATE_RECIPE_STEP_ARGUMENT)
        __consume_sequence(STATE_RECIPE_STEP, STATE_RECIPE_STEP_ENV_LIST, STATE_RECIPE_STEP_ENV)

        case STATE_RECIPE_STEP_DEPENDENCY:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_RECIPE_STEP_DEPEND_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    // TODO store the dependency name

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        
        case STATE_RECIPE_STEP_ARGUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_RECIPE_STEP_ARGUMENT_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    // TODO store the argument name

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_RECIPE_STEP_ENV:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_RECIPE_STEP_ENV_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    // TODO store the env name

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_sequence(STATE_SECTION, STATE_COMMANDS_LIST, STATE_COMMAND)

        case STATE_COMMAND:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_COMMANDS_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "name") == 0) {
                        s->state = STATE_COMMAND_NAME;
                    }
                    else if (strcmp(value, "description") == 0) {
                        s->state = STATE_COMMAND_DESCRIPTION;
                    }
                    else if (strcmp(value, "path") == 0) {
                        s->state = STATE_COMMAND_PATH;
                    }
                    else if (strcmp(value, "arguments") == 0) {
                        s->state = STATE_COMMAND_ARGUMENT_LIST;
                    }
                    else if (strcmp(value, "type") == 0) {
                        s->state = STATE_COMMAND_TYPE;
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

        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_NAME, name, __parse_command_name)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_DESCRIPTION, description, __parse_command_description)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_PATH, path, __parse_command_path)
        __consume_sequence(STATE_COMMAND, STATE_COMMAND_ARGUMENT_LIST, STATE_COMMAND_ARGUMENT)
        __consume_scalar_fn(STATE_COMMAND, STATE_COMMAND_TYPE, type, __parse_command_type)
        
        case STATE_COMMAND_ARGUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_COMMAND_ARGUMENT_LIST;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    // TODO store the argument name

                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

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

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buffer, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            fprintf(stderr, "recipe_parse: failed to parse driver configuration\n");
            return -1;
        }

        status = __consume_event(&state, &event);
        if (status) {
            fprintf(stderr, "recipe_parse: failed to parse driver configuration\n");
            return -1;
        }
        yaml_event_delete(&event);
    } while (state.state != STATE_STOP);
    return 0;
}
