/**
 * @file protecc.c
 * @brief Main implementation of protecc library
 */

#include "protecc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PROTECC_EXPORT_MAGIC 0x50524F54u
#define PROTECC_EXPORT_VERSION 2u

static protecc_error_t protecc_duplicate_pattern(
    const char* pattern,
    char** out_pattern
) {
    size_t len;
    char* copy;
    if (!pattern || !out_pattern) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    len = strlen(pattern);
    copy = malloc(len + 1);
    if (!copy) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    memcpy(copy, pattern, len + 1);
    *out_pattern = copy;
    return PROTECC_OK;
}

const char* protecc_error_string(protecc_error_t error) {
    switch (error) {
        case PROTECC_OK:
            return "Success";
        case PROTECC_ERROR_INVALID_PATTERN:
            return "Invalid pattern";
        case PROTECC_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case PROTECC_ERROR_INVALID_ARGUMENT:
            return "Invalid argument";
        case PROTECC_ERROR_COMPILE_FAILED:
            return "Compilation failed";
        default:
            return "Unknown error";
    }
}

protecc_error_t protecc_compile(
    const char** patterns,
    size_t count,
    uint32_t flags,
    protecc_compiled_t** compiled
) {
    protecc_pattern_t* entries;
    protecc_error_t err = PROTECC_OK;

    if (!patterns || !compiled || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    entries = calloc(count, sizeof(protecc_pattern_t));
    if (!entries) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        entries[i].pattern = patterns[i];
        entries[i].permissions = PROTECC_PERM_ALL;
    }

    err = protecc_compile_with_permissions(entries, count, flags, compiled);
    free(entries);
    return err;
}

protecc_error_t protecc_compile_with_permissions(
    const protecc_pattern_t* patterns,
    size_t count,
    uint32_t flags,
    protecc_compiled_t** compiled
) {
    if (!patterns || !compiled || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *compiled = NULL;
    protecc_compiled_t* comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->root = protecc_node_new(NODE_LITERAL);
    if (!comp->root) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->patterns = calloc(count, sizeof(char*));
    comp->permissions = calloc(count, sizeof(uint32_t));
    if (!comp->patterns || !comp->permissions) {
        protecc_free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->flags = flags;
    comp->stats.num_patterns = count;
    comp->pattern_count = count;

    for (size_t i = 0; i < count; i++) {
        uint32_t permissions = patterns[i].permissions;

        if (!patterns[i].pattern ||
            (permissions & ~((uint32_t)PROTECC_PERM_ALL)) != 0) {
            protecc_free(comp);
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        protecc_error_t err = protecc_parse_pattern(patterns[i].pattern, comp->root, flags);
        if (err != PROTECC_OK) {
            protecc_free(comp);
            return err;
        }

        err = protecc_duplicate_pattern(patterns[i].pattern, &comp->patterns[i]);
        if (err != PROTECC_OK) {
            protecc_free(comp);
            return err;
        }

        comp->permissions[i] = permissions;
    }

    comp->stats.num_nodes = 0;
    comp->stats.max_depth = 0;
    comp->stats.binary_size = sizeof(uint32_t) * 4;
    for (size_t i = 0; i < comp->pattern_count; i++) {
        comp->stats.binary_size += sizeof(uint32_t) * 2;
        comp->stats.binary_size += strlen(comp->patterns[i]);
    }

    *compiled = comp;
    return PROTECC_OK;
}

bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len
) {
    return protecc_match_with_permissions(compiled, path, path_len, PROTECC_PERM_NONE);
}

bool protecc_match_with_permissions(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    uint32_t required_permissions
) {
    if (!compiled || !path || !compiled->patterns || !compiled->permissions) {
        return false;
    }

    if ((required_permissions & ~((uint32_t)PROTECC_PERM_ALL)) != 0) {
        return false;
    }

    for (size_t i = 0; i < compiled->pattern_count; i++) {
        uint32_t permissions = compiled->permissions[i];
        if ((required_permissions != PROTECC_PERM_NONE) &&
            ((permissions & required_permissions) != required_permissions)) {
            continue;
        }

        if (protecc_match_pattern(compiled->patterns[i], path, path_len, compiled->flags)) {
            return true;
        }
    }
    return false;
}

protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t* stats
) {
    if (!compiled || !stats) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    *stats = compiled->stats;
    return PROTECC_OK;
}

protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_written
) {
    if (!compiled || !bytes_written) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    size_t required_size = sizeof(uint32_t) * 4;
    for (size_t i = 0; i < compiled->pattern_count; i++) {
        required_size += sizeof(uint32_t) * 2;
        required_size += strlen(compiled->patterns[i]);
    }

    *bytes_written = required_size;

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    uint8_t* out = (uint8_t*)buffer;
    uint32_t* header = (uint32_t*)out;
    header[0] = PROTECC_EXPORT_MAGIC;
    header[1] = PROTECC_EXPORT_VERSION;
    header[2] = compiled->flags;
    header[3] = (uint32_t)compiled->pattern_count;
    out += sizeof(uint32_t) * 4;

    for (size_t i = 0; i < compiled->pattern_count; i++) {
        uint32_t permissions = compiled->permissions[i];
        size_t raw_pattern_len = strlen(compiled->patterns[i]);
        if (raw_pattern_len > UINT32_MAX) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
        uint32_t pattern_len = (uint32_t)raw_pattern_len;
        memcpy(out, &permissions, sizeof(uint32_t));
        out += sizeof(uint32_t);
        memcpy(out, &pattern_len, sizeof(uint32_t));
        out += sizeof(uint32_t);
        memcpy(out, compiled->patterns[i], pattern_len);
        out += pattern_len;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
) {
    protecc_error_t err = PROTECC_OK;
    if (!buffer || !compiled || buffer_size < sizeof(uint32_t) * 4) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    const uint8_t* in = (const uint8_t*)buffer;
    const uint32_t* header = (const uint32_t*)in;
    if (header[0] != PROTECC_EXPORT_MAGIC || header[1] != PROTECC_EXPORT_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    uint32_t flags = header[2];
    uint32_t count = header[3];
    in += sizeof(uint32_t) * 4;

    protecc_pattern_t* entries = calloc(count, sizeof(protecc_pattern_t));
    char** allocated_patterns = calloc(count, sizeof(char*));
    if (!entries || !allocated_patterns) {
        err = PROTECC_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    size_t remaining = buffer_size - (sizeof(uint32_t) * 4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t permissions;
        uint32_t pattern_len;

        if (remaining < sizeof(uint32_t) * 2) {
            err = PROTECC_ERROR_INVALID_ARGUMENT;
            goto cleanup;
        }

        memcpy(&permissions, in, sizeof(uint32_t));
        in += sizeof(uint32_t);
        memcpy(&pattern_len, in, sizeof(uint32_t));
        in += sizeof(uint32_t);
        remaining -= sizeof(uint32_t) * 2;

        if (remaining < pattern_len) {
            err = PROTECC_ERROR_INVALID_ARGUMENT;
            goto cleanup;
        }

        allocated_patterns[i] = calloc((size_t)pattern_len + 1, sizeof(char));
        if (!allocated_patterns[i]) {
            err = PROTECC_ERROR_OUT_OF_MEMORY;
            goto cleanup;
        }

        memcpy(allocated_patterns[i], in, pattern_len);
        in += pattern_len;
        remaining -= pattern_len;

        entries[i].pattern = allocated_patterns[i];
        entries[i].permissions = permissions;
    }

    err = protecc_compile_with_permissions(entries, count, flags, compiled);
cleanup:
    if (allocated_patterns) {
        for (uint32_t i = 0; i < count; i++) {
            free(allocated_patterns[i]);
        }
    }
    free(allocated_patterns);
    free(entries);
    return err;
}

void protecc_free(protecc_compiled_t* compiled) {
    if (!compiled) {
        return;
    }
    
    if (compiled->root) {
        protecc_node_free(compiled->root);
    }

    if (compiled->patterns) {
        for (size_t i = 0; i < compiled->pattern_count; i++) {
            free(compiled->patterns[i]);
        }
        free(compiled->patterns);
    }

    free(compiled->permissions);
    free(compiled);
}

protecc_error_t protecc_validate_pattern(const char* pattern) {
    if (!pattern) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Basic validation - check for balanced brackets
    int bracket_depth = 0;
    for (const char* p = pattern; *p; p++) {
        if (*p == '[') {
            bracket_depth++;
        } else if (*p == ']') {
            bracket_depth--;
            if (bracket_depth < 0) {
                return PROTECC_ERROR_INVALID_PATTERN;
            }
        }
    }
    
    if (bracket_depth != 0) {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    
    return PROTECC_OK;
}
