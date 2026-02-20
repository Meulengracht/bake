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

#include "../../private.h"

static size_t __profile_dfa_size(uint32_t stateCount, uint32_t classCount, uint32_t acceptWordCount)
{
    return sizeof(protecc_profile_header_t)
        + sizeof(protecc_profile_dfa_t)
        + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        + ((size_t)acceptWordCount * sizeof(uint32_t))
        + ((size_t)stateCount * sizeof(uint32_t))
        + ((size_t)stateCount * (size_t)classCount * sizeof(uint32_t));
}

static bool __valid_dfa(const protecc_profile_t* profile) {
    if (profile == NULL || !profile->has_dfa) {
        return false;
    }
    if (profile->dfa_transitions == NULL || profile->dfa_accept == NULL || profile->dfa_perms == NULL) {
        return false;
    }
    if (profile->dfa_num_states == 0 || profile->dfa_num_classes == 0) {
        return false;
    }
    return true;
}

protecc_error_t __export_dfa_profile(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    size_t                   requiredSize;
    uint8_t*                 out;
    protecc_profile_header_t header;
    protecc_profile_dfa_t    dfa;
    uint32_t                 classmapOffset;
    uint32_t                 acceptOffset;
    uint32_t                 permissionsOffset;
    uint32_t                 transitionsOffset;

    if (!__valid_dfa(profile)) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    // Either must be provided
    if (buffer == NULL && bytesWritten == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (profile->dfa_num_classes != PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    requiredSize = __profile_dfa_size(
        profile->dfa_num_states,
        profile->dfa_num_classes,
        profile->dfa_accept_words
    );
    if (requiredSize > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (buffer == NULL) {
        return PROTECC_OK;
    }

    if (bufferSize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    classmapOffset = (uint32_t)(sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t));
    acceptOffset = classmapOffset + PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    permissionsOffset = acceptOffset + (uint32_t)(profile->dfa_accept_words * sizeof(uint32_t));
    transitionsOffset = permissionsOffset + (uint32_t)(profile->dfa_num_states * sizeof(uint32_t));

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (profile->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_DFA;
    header.num_nodes = 0;
    header.num_edges = 0;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)profile->stats.num_patterns;
    header.stats.binary_size = (uint32_t)requiredSize;
    header.stats.max_depth = (uint32_t)profile->stats.max_depth;
    header.stats.num_nodes = (uint32_t)profile->stats.num_nodes;

    memset(&dfa, 0, sizeof(dfa));
    dfa.num_states = profile->dfa_num_states;
    dfa.num_classes = profile->dfa_num_classes;
    dfa.start_state = profile->dfa_start_state;
    dfa.accept_words = profile->dfa_accept_words;
    dfa.classmap_off = classmapOffset;
    dfa.accept_off = acceptOffset;
    dfa.perms_off = permissionsOffset;
    dfa.transitions_off = transitionsOffset;

    out = (uint8_t*)buffer;
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), &dfa, sizeof(dfa));
    memcpy(out + classmapOffset, profile->dfa_classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(out + acceptOffset, profile->dfa_accept, (size_t)profile->dfa_accept_words * sizeof(uint32_t));
    memcpy(out + permissionsOffset, profile->dfa_perms, (size_t)profile->dfa_num_states * sizeof(uint32_t));
    memcpy(
        out + transitionsOffset,
        profile->dfa_transitions,
        (size_t)profile->dfa_num_states * (size_t)profile->dfa_num_classes * sizeof(uint32_t)
    );
    return PROTECC_OK;
}

static protecc_error_t __validate_import_dfa_layout(
    const protecc_profile_dfa_t* dfa,
    size_t                       bufferSize,
    uint32_t                     headerBinarySize,
    size_t*                      transitionsSize,
    size_t*                      acceptSize,
    size_t*                      permissionsSize)
{
    size_t transitionsCount;
    size_t requiredSize;

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > 256u) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->start_state >= dfa->num_states) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    transitionsCount = (size_t)dfa->num_states * (size_t)dfa->num_classes;
    *transitionsSize = transitionsCount * sizeof(uint32_t);
    *acceptSize = (size_t)dfa->accept_words * sizeof(uint32_t);
    *permissionsSize = (size_t)dfa->num_states * sizeof(uint32_t);
    requiredSize = __profile_dfa_size(dfa->num_states, dfa->num_classes, dfa->accept_words);

    if (bufferSize < requiredSize || headerBinarySize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((size_t)dfa->classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE > requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->accept_off & 3u) != 0u || (size_t)dfa->accept_off + *acceptSize > requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->perms_off & 3u) != 0u || (size_t)dfa->perms_off + *permissionsSize > requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->transitions_off & 3u) != 0u || (size_t)dfa->transitions_off + *transitionsSize > requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __allocate_import_dfa_buffers(
    size_t              acceptSize,
    size_t              permissionsSize,
    size_t              transitionsSize,
    protecc_profile_t** profileOut)
{
    protecc_profile_t* profile;

    profile = calloc(1, sizeof(protecc_profile_t));
    if (profile == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    profile->dfa_accept = malloc(acceptSize);
    profile->dfa_perms = malloc(permissionsSize);
    profile->dfa_transitions = malloc(transitionsSize);
    if (profile->dfa_accept == NULL || profile->dfa_perms == NULL || profile->dfa_transitions == NULL) {
        free(profile->dfa_transitions);
        free(profile->dfa_perms);
        free(profile->dfa_accept);
        free(profile);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    *profileOut = profile;
    return PROTECC_OK;
}

protecc_error_t __import_dfa_profile(
    const uint8_t*                  base,
    size_t                          buffer_size,
    const protecc_profile_header_t* header,
    protecc_profile_t**             profileOut)
{
    protecc_profile_dfa_t dfa;
    protecc_profile_t*    profile;
    size_t                transitionsSize;
    size_t                acceptSize;
    size_t                permissionsSize;
    protecc_error_t       err;

    if (buffer_size < sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(&dfa, base + sizeof(protecc_profile_header_t), sizeof(dfa));

    err = __validate_import_dfa_layout(
        &dfa,
        buffer_size,
        header->stats.binary_size,
        &transitionsSize,
        &acceptSize,
        &permissionsSize
    );
    if (err != PROTECC_OK) {
        return err;
    }

    err = __allocate_import_dfa_buffers(acceptSize, permissionsSize, transitionsSize, &profile);
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(profile->dfa_classmap, base + dfa.classmap_off, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(profile->dfa_accept, base + dfa.accept_off, acceptSize);
    memcpy(profile->dfa_perms, base + dfa.perms_off, permissionsSize);
    memcpy(profile->dfa_transitions, base + dfa.transitions_off, transitionsSize);

    profile->has_dfa = true;
    profile->dfa_num_states = dfa.num_states;
    profile->dfa_num_classes = dfa.num_classes;
    profile->dfa_start_state = dfa.start_state;
    profile->dfa_accept_words = dfa.accept_words;

    profile->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    profile->stats.num_patterns = header->stats.num_patterns;
    profile->stats.binary_size = header->stats.binary_size;
    profile->stats.max_depth = header->stats.max_depth;
    profile->stats.num_nodes = header->stats.num_nodes;

    protecc_compile_config_default(&profile->config);
    profile->config.mode = PROTECC_COMPILE_MODE_DFA;

    *profileOut = profile;
    return PROTECC_OK;
}
