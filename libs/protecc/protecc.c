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

#include <protecc/profile.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "private.h"

static void protecc_collect_nodes(
    const protecc_node_t* node,
    const protecc_node_t** nodes,
    size_t* index
) {
    if (node == NULL) {
        return;
    }

    nodes[(*index)++] = node;
    for (size_t i = 0; i < node->num_children; i++) {
        protecc_collect_nodes(node->children[i], nodes, index);
    }
}

static size_t protecc_find_node_index(
    const protecc_node_t* const* nodes,
    size_t                       count,
    const protecc_node_t*        target)
{
    for (size_t i = 0; i < count; i++) {
        if (nodes[i] == target) {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t protecc_profile_size(uint32_t num_nodes, uint32_t num_edges) {
    return sizeof(protecc_profile_header_t)
        + (size_t)num_nodes * sizeof(protecc_profile_node_t)
        + (size_t)num_edges * sizeof(uint32_t);
}

static size_t protecc_profile_dfa_size(uint32_t num_states, uint32_t num_classes, uint32_t accept_words) {
    return sizeof(protecc_profile_header_t)
        + sizeof(protecc_profile_dfa_t)
        + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        + ((size_t)accept_words * sizeof(uint32_t))
        + ((size_t)num_states * sizeof(uint32_t))
        + ((size_t)num_states * (size_t)num_classes * sizeof(uint32_t));
}

static bool __match_dfa(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    protecc_permission_t* perms_out
) {
    uint32_t state;

    if (!compiled || !compiled->has_dfa || !compiled->dfa_transitions || !compiled->dfa_accept ||
        !compiled->dfa_perms || !path || !perms_out) {
        return false;
    }

    *perms_out = PROTECC_PERM_NONE;

    state = compiled->dfa_start_state;
    for (size_t i = 0; i < path_len; i++) {
        uint8_t c = (uint8_t)path[i];
        uint32_t cls = compiled->dfa_classmap[c];
        uint64_t index;

        if (cls >= compiled->dfa_num_classes) {
            return false;
        }

        index = ((uint64_t)state * (uint64_t)compiled->dfa_num_classes) + (uint64_t)cls;
        state = compiled->dfa_transitions[index];
        if (state >= compiled->dfa_num_states) {
            return false;
        }
    }

    if ((compiled->dfa_accept[state >> 5] & (1u << (state & 31u))) == 0u) {
        return false;
    }

    *perms_out = (protecc_permission_t)compiled->dfa_perms[state];
    return true;
}

void protecc_compile_config_default(protecc_compile_config_t* config) {
    if (!config) {
        return;
    }

    config->mode = PROTECC_COMPILE_MODE_TRIE;
    config->max_patterns = 256;
    config->max_pattern_length = 128;
    config->max_states = 2048;
    config->max_classes = 32;
}

static protecc_error_t protecc_update_stats(protecc_compiled_t* compiled)
{
    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t num_edges = 0;

    if (!compiled || !compiled->root) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    protecc_node_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);

    if (num_nodes > UINT32_MAX || num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    compiled->stats.num_nodes = num_nodes;
    compiled->stats.max_depth = max_depth;
    compiled->stats.binary_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);

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

static protecc_error_t __resolve_compile_config(
    const protecc_compile_config_t* input,
    protecc_compile_config_t* local,
    const protecc_compile_config_t** cfg_out)
{
    if (!local || !cfg_out) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (input == NULL) {
        protecc_compile_config_default(local);
        *cfg_out = local;
    } else {
        *cfg_out = input;
    }

    if ((*cfg_out)->mode != PROTECC_COMPILE_MODE_TRIE && (*cfg_out)->mode != PROTECC_COMPILE_MODE_DFA) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if ((*cfg_out)->max_patterns == 0 || (*cfg_out)->max_pattern_length == 0 ||
        (*cfg_out)->max_states == 0 || (*cfg_out)->max_classes == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_compile_inputs(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    const protecc_compile_config_t* cfg)
{
    if (!patterns || !cfg || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (count > cfg->max_patterns) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < count; i++) {
        size_t pattern_length;

        if (patterns[i].pattern == NULL) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        pattern_length = strlen(patterns[i].pattern);
        if (pattern_length > cfg->max_pattern_length) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __build_trie_patterns(
    protecc_compiled_t*             comp,
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags)
{
    if (!comp || !patterns) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    comp->root = protecc_node_new(NODE_LITERAL);
    if (!comp->root) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        protecc_node_t* terminal = NULL;
        protecc_error_t err = protecc_parse_pattern(patterns[i].pattern, comp->root, flags, &terminal);
        if (err != PROTECC_OK) {
            return err;
        }
        if (terminal) {
            terminal->perms |= patterns[i].perms;
        }
    }
    return PROTECC_OK;
}

static protecc_error_t __finalize_compilation(protecc_compiled_t* comp) {
    protecc_error_t stats_err;

    if (!comp) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    stats_err = protecc_update_stats(comp);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (comp->stats.num_nodes > comp->config.max_states) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (comp->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return protecc_dfa_from_trie(comp);
    }

    return PROTECC_OK;
}

protecc_error_t protecc_compile(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags,
    const protecc_compile_config_t* config,
    protecc_compiled_t**            compiled
) {
    protecc_compile_config_t local_config;
    const protecc_compile_config_t* cfg;
    protecc_error_t config_err;
    protecc_error_t input_err;
    protecc_error_t build_err;
    protecc_error_t finalize_err;
    protecc_compiled_t* comp;

    if (compiled == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    config_err = __resolve_compile_config(config, &local_config, &cfg);
    if (config_err != PROTECC_OK) {
        return config_err;
    }

    input_err = __validate_compile_inputs(patterns, count, cfg);
    if (input_err != PROTECC_OK) {
        return input_err;
    }
    
    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->flags = flags;
    comp->config = *cfg;
    comp->stats.num_patterns = count;

    build_err = __build_trie_patterns(comp, patterns, count, flags);
    if (build_err != PROTECC_OK) {
        protecc_free(comp);
        return build_err;
    }

    finalize_err = __finalize_compilation(comp);
    if (finalize_err != PROTECC_OK) {
        protecc_free(comp);
        return finalize_err;
    }
    
    *compiled = comp;
    return PROTECC_OK;
}

bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    protecc_permission_t* perms_out
) {
    if (!perms_out) {
        return false;
    }

    *perms_out = PROTECC_PERM_NONE;

    if (!compiled || !path) {
        return false;
    }
    
    if (path_len == 0) {
        path_len = strlen(path);
    }
    
    if (compiled->has_dfa) {
        return __match_dfa(compiled, path, path_len, perms_out);
    }

    if (!compiled->root) {
        return false;
    }

    return protecc_match_internal(compiled->root, path, path_len, 0, compiled->flags, perms_out);
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

static void __free_import_nodes(protecc_node_t** nodes, uint32_t count) {
    if (nodes == NULL) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (nodes[i]) {
            free(nodes[i]->children);
            free(nodes[i]);
        }
    }
    free(nodes);
}

static protecc_error_t __cleanup_import_trie_failure(
    protecc_node_t** nodes,
    uint32_t num_nodes,
    protecc_compiled_t* comp,
    protecc_error_t error)
{
    __free_import_nodes(nodes, num_nodes);
    free(comp);
    return error;
}

static protecc_error_t __read_and_validate_profile_header(
    const void* buffer,
    size_t buffer_size,
    protecc_profile_header_t* header)
{
    if (!buffer || !header || buffer_size < sizeof(protecc_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(header, buffer, sizeof(*header));

    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if ((header->flags & (PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA)) == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_import_dfa_layout(
    const protecc_profile_dfa_t* dfa,
    size_t buffer_size,
    uint32_t header_binary_size,
    size_t* transitions_size,
    size_t* accept_size,
    size_t* perms_size)
{
    size_t transitions_count;
    size_t required_size;

    if (!dfa || !transitions_size || !accept_size || !perms_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > 256u) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->start_state >= dfa->num_states) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    transitions_count = (size_t)dfa->num_states * (size_t)dfa->num_classes;
    *transitions_size = transitions_count * sizeof(uint32_t);
    *accept_size = (size_t)dfa->accept_words * sizeof(uint32_t);
    *perms_size = (size_t)dfa->num_states * sizeof(uint32_t);
    required_size = protecc_profile_dfa_size(dfa->num_states, dfa->num_classes, dfa->accept_words);

    if (buffer_size < required_size || header_binary_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((size_t)dfa->classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->accept_off & 3u) != 0u || (size_t)dfa->accept_off + *accept_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->perms_off & 3u) != 0u || (size_t)dfa->perms_off + *perms_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->transitions_off & 3u) != 0u || (size_t)dfa->transitions_off + *transitions_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __allocate_import_dfa_buffers(
    size_t accept_size,
    size_t perms_size,
    size_t transitions_size,
    protecc_compiled_t** compiled_out)
{
    protecc_compiled_t* comp;

    if (!compiled_out) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->dfa_accept = malloc(accept_size);
    comp->dfa_perms = malloc(perms_size);
    comp->dfa_transitions = malloc(transitions_size);
    if (!comp->dfa_accept || !comp->dfa_perms || !comp->dfa_transitions) {
        free(comp->dfa_transitions);
        free(comp->dfa_perms);
        free(comp->dfa_accept);
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    *compiled_out = comp;
    return PROTECC_OK;
}

static protecc_error_t __export_dfa_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t required_size;
    uint8_t* out;
    protecc_profile_header_t header;
    protecc_profile_dfa_t dfa;
    uint32_t classmap_off;
    uint32_t accept_off;
    uint32_t perms_off;
    uint32_t transitions_off;

    if (!compiled->has_dfa || !compiled->dfa_transitions || !compiled->dfa_accept || !compiled->dfa_perms) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (compiled->dfa_num_classes != 256u) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    required_size = protecc_profile_dfa_size(
        compiled->dfa_num_states,
        compiled->dfa_num_classes,
        compiled->dfa_accept_words);
    if (required_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    classmap_off = (uint32_t)(sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t));
    accept_off = classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    perms_off = accept_off + (uint32_t)(compiled->dfa_accept_words * sizeof(uint32_t));
    transitions_off = perms_off + (uint32_t)(compiled->dfa_num_states * sizeof(uint32_t));

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (compiled->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_DFA;
    header.num_nodes = 0;
    header.num_edges = 0;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)required_size;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;

    memset(&dfa, 0, sizeof(dfa));
    dfa.num_states = compiled->dfa_num_states;
    dfa.num_classes = compiled->dfa_num_classes;
    dfa.start_state = compiled->dfa_start_state;
    dfa.accept_words = compiled->dfa_accept_words;
    dfa.classmap_off = classmap_off;
    dfa.accept_off = accept_off;
    dfa.perms_off = perms_off;
    dfa.transitions_off = transitions_off;

    out = (uint8_t*)buffer;
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), &dfa, sizeof(dfa));
    memcpy(out + classmap_off, compiled->dfa_classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(out + accept_off, compiled->dfa_accept, (size_t)compiled->dfa_accept_words * sizeof(uint32_t));
    memcpy(out + perms_off, compiled->dfa_perms, (size_t)compiled->dfa_num_states * sizeof(uint32_t));
    memcpy(out + transitions_off,
           compiled->dfa_transitions,
           (size_t)compiled->dfa_num_states * (size_t)compiled->dfa_num_classes * sizeof(uint32_t));
    return PROTECC_OK;
}

static protecc_error_t __export_trie_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t num_edges = 0;
    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t required_size;
    const protecc_node_t** nodes;
    size_t index;
    uint8_t* out;
    protecc_profile_header_t header;
    protecc_profile_node_t* profile_nodes;
    uint32_t* edges;
    size_t edge_offset;

    if (compiled->stats.num_nodes > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_node_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);
    if (num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    required_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);
    if (required_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(num_nodes, sizeof(*nodes));
    if (nodes == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    index = 0;
    protecc_collect_nodes(compiled->root, nodes, &index);
    if (index != num_nodes) {
        free(nodes);
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    out = (uint8_t*)buffer;
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (compiled->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_TRIE;
    header.num_nodes = (uint32_t)num_nodes;
    header.num_edges = (uint32_t)num_edges;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)required_size;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;
    memcpy(out, &header, sizeof(header));

    profile_nodes = (protecc_profile_node_t*)(out + sizeof(header));
    edges = (uint32_t*)(out + sizeof(header) + num_nodes * sizeof(protecc_profile_node_t));

    edge_offset = 0;
    for (size_t i = 0; i < num_nodes; i++) {
        const protecc_node_t* node = nodes[i];
        protecc_profile_node_t profile = {0};
        profile.type = (uint8_t)node->type;
        profile.modifier = (uint8_t)node->modifier;
        profile.is_terminal = node->is_terminal ? 1 : 0;
        profile.child_start = (uint32_t)edge_offset;
        profile.child_count = (uint16_t)node->num_children;
        profile.perms = (uint32_t)node->perms;

        if (node->num_children > UINT16_MAX) {
            free(nodes);
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        switch (node->type) {
            case NODE_LITERAL:
                profile.data.literal = (uint8_t)node->data.literal;
                break;
            case NODE_RANGE:
                profile.data.range.start = (uint8_t)node->data.range.start;
                profile.data.range.end = (uint8_t)node->data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(profile.data.charset, node->data.charset.chars, sizeof(profile.data.charset));
                break;
            default:
                break;
        }

        profile_nodes[i] = profile;

        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_index = protecc_find_node_index(nodes, num_nodes, node->children[c]);
            if (child_index == SIZE_MAX || child_index > UINT32_MAX) {
                free(nodes);
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            edges[edge_offset++] = (uint32_t)child_index;
        }
    }

    free(nodes);
    return PROTECC_OK;
}

static protecc_error_t __import_dfa_profile(
    const uint8_t*                  base,
    size_t                          buffer_size,
    const protecc_profile_header_t* header,
    protecc_compiled_t**            compiled)
{
    protecc_profile_dfa_t dfa;
    protecc_compiled_t* comp;
    protecc_error_t err;
    size_t transitions_size;
    size_t accept_size;
    size_t perms_size;

    if (buffer_size < sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(&dfa, base + sizeof(protecc_profile_header_t), sizeof(dfa));

    err = __validate_import_dfa_layout(&dfa, buffer_size, header->stats.binary_size,
                                       &transitions_size, &accept_size, &perms_size);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __allocate_import_dfa_buffers(accept_size, perms_size, transitions_size, &comp);
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(comp->dfa_classmap, base + dfa.classmap_off, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(comp->dfa_accept, base + dfa.accept_off, accept_size);
    memcpy(comp->dfa_perms, base + dfa.perms_off, perms_size);
    memcpy(comp->dfa_transitions, base + dfa.transitions_off, transitions_size);

    comp->has_dfa = true;
    comp->dfa_num_states = dfa.num_states;
    comp->dfa_num_classes = dfa.num_classes;
    comp->dfa_start_state = dfa.start_state;
    comp->dfa_accept_words = dfa.accept_words;

    comp->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    protecc_compile_config_default(&comp->config);
    comp->config.mode = PROTECC_COMPILE_MODE_DFA;
    comp->stats.num_patterns = header->stats.num_patterns;
    comp->stats.binary_size = header->stats.binary_size;
    comp->stats.max_depth = header->stats.max_depth;
    comp->stats.num_nodes = header->stats.num_nodes;

    *compiled = comp;
    return PROTECC_OK;
}

static protecc_error_t __import_trie_profile(
    const uint8_t*                  base,
    size_t                          buffer_size,
    const protecc_profile_header_t* header,
    protecc_compiled_t**            compiled)
{
    size_t required_size = protecc_profile_size(header->num_nodes, header->num_edges);
    const protecc_profile_node_t* profile_nodes;
    const uint32_t* edges;
    protecc_compiled_t* comp;
    protecc_node_t** nodes;

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    profile_nodes = (const protecc_profile_node_t*)(base + sizeof(protecc_profile_header_t));
    edges = (const uint32_t*)(base + sizeof(protecc_profile_header_t)
                              + (size_t)header->num_nodes * sizeof(protecc_profile_node_t));

    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    if (header->root_index >= header->num_nodes) {
        free(comp);
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(header->num_nodes, sizeof(*nodes));
    if (nodes == NULL) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = protecc_node_new((protecc_node_type_t)profile_nodes[i].type);
        if (node == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->modifier = (protecc_modifier_t)profile_nodes[i].modifier;
        node->is_terminal = profile_nodes[i].is_terminal != 0;
        node->perms = (protecc_permission_t)profile_nodes[i].perms;

        switch (node->type) {
            case NODE_LITERAL:
                node->data.literal = (char)profile_nodes[i].data.literal;
                break;
            case NODE_RANGE:
                node->data.range.start = (char)profile_nodes[i].data.range.start;
                node->data.range.end = (char)profile_nodes[i].data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(node->data.charset.chars, profile_nodes[i].data.charset,
                       sizeof(node->data.charset.chars));
                break;
            default:
                break;
        }

        nodes[i] = node;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = nodes[i];
        uint16_t child_count = profile_nodes[i].child_count;
        uint32_t child_start = profile_nodes[i].child_start;

        if (child_count == 0) {
            continue;
        }

        if ((uint32_t)child_start + child_count > header->num_edges) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_INVALID_ARGUMENT);
        }

        node->children = calloc(child_count, sizeof(*node->children));
        if (node->children == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->capacity_children = child_count;
        node->num_children = child_count;
        for (uint16_t c = 0; c < child_count; c++) {
            uint32_t child_index = edges[child_start + c];
            if (child_index >= header->num_nodes) {
                return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                     PROTECC_ERROR_INVALID_ARGUMENT);
            }
            node->children[c] = nodes[child_index];
        }
    }

    comp->root = nodes[header->root_index];
    comp->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    protecc_compile_config_default(&comp->config);
    comp->stats.num_patterns = header->stats.num_patterns;
    comp->stats.binary_size = header->stats.binary_size;
    comp->stats.max_depth = header->stats.max_depth;
    comp->stats.num_nodes = header->stats.num_nodes;

    free(nodes);
    *compiled = comp;
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

    protecc_error_t stats_err = protecc_update_stats((protecc_compiled_t*)compiled);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (compiled->stats.num_patterns > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (compiled->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return __export_dfa_profile(compiled, buffer, buffer_size, bytes_written);
    }
    return __export_trie_profile(compiled, buffer, buffer_size, bytes_written);
}

protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
) {
    const uint8_t*           base = (const uint8_t*)buffer;
    protecc_profile_header_t header;
    protecc_error_t          header_err;

    if (!compiled || buffer_size < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    header_err = __read_and_validate_profile_header(buffer, buffer_size, &header);
    if (header_err != PROTECC_OK) {
        return header_err;
    }

    if ((header.flags & PROTECC_PROFILE_FLAG_TYPE_DFA) != 0) {
        return __import_dfa_profile(base, buffer_size, &header, compiled);
    }
    return __import_trie_profile(base, buffer_size, &header, compiled);
}

void protecc_free(protecc_compiled_t* compiled) {
    if (!compiled) {
        return;
    }

    free(compiled->dfa_accept);
    free(compiled->dfa_perms);
    free(compiled->dfa_transitions);
    
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
