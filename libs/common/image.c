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
#include <chef/image.h>
#include <yaml/yaml.h>
#include <stdio.h>
#include <stdlib.h>

enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_DISK_SCHEMA,

    STATE_PARTITIONS_LIST,

    STATE_PARTITION,           // MAPPING_START
    STATE_PARTITION_LABEL,
    STATE_PARTITION_TYPE,
    STATE_PARTITION_ID,
    STATE_PARTITION_SIZE,
    STATE_PARTITION_CONTENT,
    STATE_PARTITION_ATTRIBUTES_LIST,
    STATE_PARTITION_SOURCES_LIST,

    STATE_PARTITION_ATTRIBUTE, // MAPPING_START

    STATE_PARTITION_FAT_OPTIONS,
    STATE_PARTITION_FAT_OPTIONS_RESERVED_IMAGE,

    STATE_PARTITION_SOURCE,    // MAPPING_START
    STATE_PARTITION_SOURCE_TYPE,
    STATE_PARTITION_SOURCE_PATH,
    STATE_PARTITION_SOURCE_TARGET,

    STATE_STOP
};

struct parser_state {
    enum state                         states[32];
    int                                state_index;
    enum state                         state;
    struct chef_image                  image;
    struct chef_image_partition        partition;
    struct chef_image_partition_source source;
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

static long long __parse_integer(const char* value)
{
    if (value == NULL || strlen(value) == 0) {
        return 0;
    }
    return strtoll(value, NULL, 10);
}

static enum chef_image_source_type __parse_source_type(const char* value)
{
    if (strcmp(value, "file") == 0) {
        return CHEF_IMAGE_SOURCE_FILE;
    } else if (strcmp(value, "dir") == 0) {
        return CHEF_IMAGE_SOURCE_DIRECTORY;
    } else if (strcmp(value, "package") == 0) {
        return CHEF_IMAGE_SOURCE_PACKAGE;
    } else if (strcmp(value, "raw") == 0) {
        return CHEF_IMAGE_SOURCE_RAW;
    } else {
        return CHEF_IMAGE_SOURCE_INVALID;
    }
}

static enum chef_image_schema __parse_disk_schema(const char* value)
{
    if (strcmp(value, "mbr") == 0) {
        return CHEF_IMAGE_SCHEMA_MBR;
    } else if (strcmp(value, "gpt") == 0) {
        return CHEF_IMAGE_SCHEMA_GPT;
    } else {
        return CHEF_IMAGE_SCHEMA_INVALID;
    }
}

static void __finalize_image(struct parser_state* state)
{
    // Check schema was set
    if (state->image.schema == CHEF_IMAGE_SCHEMA_INVALID) {
        fprintf(stderr, "parse error: 'schema' must be set\n");
        exit(EXIT_FAILURE);
    }

    // Check that size is set for all but n-1 partitions
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

static void __finalize_partition(struct parser_state* state)
{
    struct chef_image_partition* partition;

    // verify required project members
    if (__is_valid_name(state->partition.label)) {
        fprintf(stderr, "parse error: partition 'label' must be provided and only contain [a-zA-Z_-]\n");
        exit(EXIT_FAILURE);
    }

    if (state->partition.fstype == NULL) {
        fprintf(stderr, "parse error: partition 'type' is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->partition.guid == NULL && state->image.schema == CHEF_IMAGE_SCHEMA_GPT) {
        fprintf(stderr, "parse error: partition 'guid' is required\n");
        exit(EXIT_FAILURE);
    }

    // now we copy and reset
    partition = malloc(sizeof(struct chef_image_partition));
    if (partition == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    // copy the set values
    memcpy(partition, &state->partition, sizeof(struct chef_image_partition));
    list_add(&state->image.partitions, &partition->list_header);

    // reset the structure in state
    memset(&state->partition, 0, sizeof(struct chef_image_partition));
}

static void __finalize_source(struct parser_state* state)
{
    struct chef_image_partition_source* source;

    if (state->source.type == CHEF_IMAGE_SOURCE_INVALID) {
        fprintf(stderr, "parse error: source 'type' is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->source.source == NULL) {
        fprintf(stderr, "parse error: source member 'source' is required\n");
        exit(EXIT_FAILURE);
    }

    if (state->source.target == NULL) {
        fprintf(stderr, "parse error: source member 'target' is required\n");
        exit(EXIT_FAILURE);
    }

    // now we copy and reset
    source = malloc(sizeof(struct chef_image_partition_source));
    if (source == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }

    // copy the set values
    memcpy(source, &state->source, sizeof(struct chef_image_partition_source));
    list_add(&state->partition.sources, &source->list_header);

    // reset the structure in state
    memset(&state->source, 0, sizeof(struct chef_image_partition_source));
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

DEFINE_LIST_STRING_ADD(partitions, partition, attributes)

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

static int __parse_guid_and_type(const char* id, char** guid, unsigned char* type)
{
    char* p;

    // XX = type
    // XXXXXXX... = guid
    // XX, XXXXXX... = type & guid
    if (strlen(id) == 2) {
        *type = (uint8_t)strtoul(id, &p, 16);
        return *type != 0 ? 0 : -1;
    }
    
    p = strchr(id, ',');
    if (p != NULL) {
        // both type and guid, parse type first
        *type = (uint8_t)strtoul(id, &p, 16);
        // skip past comma and whitespace
        while (*p == ',' || *p == ' ') p++;
    } else {
        p = (char*)id;
    }

    *guid = platform_strdup(p);
    return 0;
}

static int __consume_event(struct parser_state* s, yaml_event_t* event)
{
    char *value;
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
                    if (strcmp(value, "schema") == 0) {
                        __parser_push_state(s, STATE_DISK_SCHEMA);
                    } else if (strcmp(value, "partitions") == 0) {
                        __parser_push_state(s, STATE_PARTITIONS_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_SECTION) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                
                case YAML_MAPPING_END_EVENT:
                    __finalize_image(s);
                    __parser_pop_state(s);
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


        __consume_scalar_fn(STATE_DISK_SCHEMA, image.schema, __parse_disk_schema)
        __consume_sequence_mapped(STATE_PARTITIONS_LIST, STATE_PARTITION)

        case STATE_PARTITION:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_partition(s);
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "label") == 0) {
                        __parser_push_state(s, STATE_PARTITION_LABEL);
                    } else if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_PARTITION_TYPE);
                    } else if (strcmp(value, "id") == 0) {
                        __parser_push_state(s, STATE_PARTITION_ID);
                    } else if (strcmp(value, "size") == 0) {
                        __parser_push_state(s, STATE_PARTITION_SIZE);
                    } else if (strcmp(value, "content") == 0) {
                        __parser_push_state(s, STATE_PARTITION_CONTENT);
                    } else if (strcmp(value, "attributes") == 0) {
                        __parser_push_state(s, STATE_PARTITION_ATTRIBUTES_LIST);
                    } else if (strcmp(value, "fat-options") == 0) {
                        __parser_push_state(s, STATE_PARTITION_FAT_OPTIONS);
                    } else if (strcmp(value, "sources") == 0) {
                        __parser_push_state(s, STATE_PARTITION_SOURCES_LIST);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PARTITION) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_PARTITION_ID:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value; \
                    if (__parse_guid_and_type(value, (char**)&s->partition.guid, &s->partition.type)) {
                        fprintf(stderr, "__consume_event: partition %s: invalid type %s format\n", s->partition.label, value);
                        return -1;
                    }
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        
        __consume_scalar_fn(STATE_PARTITION_LABEL, partition.label, __parse_string)
        __consume_scalar_fn(STATE_PARTITION_TYPE, partition.fstype, __parse_string)
        __consume_scalar_fn(STATE_PARTITION_SIZE, partition.size, __parse_integer)
        __consume_scalar_fn(STATE_PARTITION_CONTENT, partition.content, __parse_string)

        __consume_sequence_unmapped(STATE_PARTITION_ATTRIBUTES_LIST, __add_partitions_attributes)
        __consume_sequence_mapped(STATE_PARTITION_SOURCES_LIST, STATE_PARTITION_SOURCE)

        case STATE_PARTITION_FAT_OPTIONS:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    break;
                case YAML_MAPPING_END_EVENT:
                    __parser_pop_state(s);
                    break;
                
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "reserved-image") == 0) {
                        __parser_push_state(s, STATE_PARTITION_FAT_OPTIONS_RESERVED_IMAGE);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PARTITION_FAT_OPTIONS) unexpected scalar: %s.\n", value);
                        return -1;
                    }
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;
        
        __consume_scalar_fn(STATE_PARTITION_FAT_OPTIONS_RESERVED_IMAGE, partition.options.fat.reserved_image, __parse_string)

        case STATE_PARTITION_SOURCE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        __parser_push_state(s, STATE_PARTITION_SOURCE_TYPE);
                    } else if (strcmp(value, "source") == 0) {
                        __parser_push_state(s, STATE_PARTITION_SOURCE_PATH);
                    } else if (strcmp(value, "target") == 0) {
                        __parser_push_state(s, STATE_PARTITION_SOURCE_TARGET);
                    } else {
                        fprintf(stderr, "__consume_event: (STATE_PARTITION_SOURCE) unexpected scalar: %s.\n", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    __finalize_source(s);
                    __parser_pop_state(s);
                    break;
                default:
                    fprintf(stderr, "__consume_event: unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        __consume_scalar_fn(STATE_PARTITION_SOURCE_TYPE, source.type, __parse_source_type)
        __consume_scalar_fn(STATE_PARTITION_SOURCE_PATH, source.source, __parse_string)
        __consume_scalar_fn(STATE_PARTITION_SOURCE_TARGET, source.target, __parse_string)

        case STATE_STOP:
            break;
    }
    return 0;
}

int chef_image_parse(void* buffer, size_t length, struct chef_image** imageOut)
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

    // create the recipe and copy all data
    *imageOut = malloc(sizeof(struct chef_image));
    if (!*imageOut) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    memcpy(*imageOut, &state.image, sizeof(struct chef_image));
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

static void __destroy_source(struct chef_image_partition_source* source)
{
    free((void*)source->source);
    free((void*)source->target);
    free(source);
}

static void __destroy_fat(struct chef_image_partition_fat_options* fatOptions)
{
    free((void*)fatOptions->reserved_image);
}

static void __destroy_partition(struct chef_image_partition* partition)
{
    // cleanup fs specific options
    if (partition->fstype != NULL && (strncmp(partition->fstype, "fat", 3) == 0)) {
        __destroy_fat(&partition->options.fat);
    }

    __destroy_list(string, partition->attributes.head, struct list_item_string);
    __destroy_list(source, partition->sources.head, struct chef_image_partition_source);
    free((void*)partition->label);
    free((void*)partition->fstype);
    free((void*)partition->guid);
    free((void*)partition->content);
    free(partition);
}

void chef_image_destroy(struct chef_image* image)
{
    if (image == NULL) {
        return;
    }
    
    __destroy_list(partition, image->partitions.head, struct chef_image_partition);
    free(image);
}
