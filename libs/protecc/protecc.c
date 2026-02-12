/**
 * @file protecc.c
 * @brief Main implementation of protecc library
 */

#include "protecc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    if (!patterns || !compiled || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    protecc_compiled_t* comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }
    
    comp->root = protecc_node_new(NODE_LITERAL);
    if (!comp->root) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }
    
    comp->flags = flags;
    comp->stats.num_patterns = count;
    
    // Parse each pattern and add to trie
    for (size_t i = 0; i < count; i++) {
        protecc_error_t err = protecc_parse_pattern(patterns[i], comp->root, flags);
        if (err != PROTECC_OK) {
            protecc_free(comp);
            return err;
        }
    }
    
    // Calculate statistics
    comp->stats.num_nodes = 0;
    comp->stats.max_depth = 0;
    // TODO: Traverse tree to count nodes and depth
    
    *compiled = comp;
    return PROTECC_OK;
}

bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len
) {
    if (!compiled || !compiled->root || !path) {
        return false;
    }
    
    if (path_len == 0) {
        path_len = strlen(path);
    }
    
    return protecc_match_internal(compiled->root, path, path_len, 0, compiled->flags);
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
    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Calculate required size
    size_t required_size = sizeof(uint32_t) * 3; // magic, version, flags
    required_size += sizeof(protecc_stats_t);
    // TODO: Add size for serialized trie
    
    if (bytes_written) {
        *bytes_written = required_size;
    }
    
    if (!buffer) {
        // Just querying size
        return PROTECC_OK;
    }
    
    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // TODO: Implement serialization
    uint32_t* header = (uint32_t*)buffer;
    header[0] = 0x50524F54; // "PROT" magic
    header[1] = 1;          // version
    header[2] = compiled->flags;
    
    return PROTECC_OK;
}

protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
) {
    if (!buffer || !compiled || buffer_size < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    const uint32_t* header = (const uint32_t*)buffer;
    if (header[0] != 0x50524F54) { // "PROT" magic
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // TODO: Implement deserialization
    return PROTECC_ERROR_COMPILE_FAILED;
}

void protecc_free(protecc_compiled_t* compiled) {
    if (!compiled) {
        return;
    }
    
    if (compiled->root) {
        protecc_node_free(compiled->root);
    }
    
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
