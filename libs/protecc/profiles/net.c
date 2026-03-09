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

#include "../private.h"

static bool __is_valid_net_protocol(protecc_net_protocol_t protocol)
{
    return protocol == PROTECC_NET_PROTOCOL_ANY
        || protocol == PROTECC_NET_PROTOCOL_TCP
        || protocol == PROTECC_NET_PROTOCOL_UDP
        || protocol == PROTECC_NET_PROTOCOL_UNIX;
}

static bool __is_valid_net_family(protecc_net_family_t family)
{
    return family == PROTECC_NET_FAMILY_ANY
        || family == PROTECC_NET_FAMILY_IPV4
        || family == PROTECC_NET_FAMILY_IPV6
        || family == PROTECC_NET_FAMILY_UNIX;
}

static protecc_error_t __validate_blob_string_offset(
    uint32_t       offset,
    const uint8_t* strings,
    size_t         stringsSize)
{
    const uint8_t* end;

    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return PROTECC_OK;
    }

    if (!strings || offset >= stringsSize) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    end = memchr(strings + offset, '\0', stringsSize - offset);
    if (!end) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    return PROTECC_OK;
}

size_t __net_dfa_block_size(const protecc_rule_dfa_runtime_t* dfa)
{
    size_t base;

    if (dfa == NULL || !dfa->present) {
        return 0;
    }

    base = sizeof(protecc_profile_dfa_t);
    base += PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    base += (size_t)dfa->accept_words * sizeof(uint32_t);
    base += (size_t)dfa->num_states * sizeof(uint32_t); /* candidate_index */
    base += (size_t)dfa->num_states * sizeof(uint32_t); /* candidate_count */
    base += (size_t)dfa->candidates_total * sizeof(uint32_t);
    base += (size_t)dfa->num_states * (size_t)dfa->num_classes * sizeof(uint32_t);
    return base;
}

protecc_error_t __net_export_dfa_block(
    const protecc_rule_dfa_runtime_t* dfa,
    uint8_t*                         base,
    size_t                           bufferSize,
    uint32_t                         offset)
{
    protecc_profile_dfa_t header;
    uint8_t*          out;
    size_t            required;
    size_t            classmap_off;
    size_t            accept_off;
    size_t            cand_index_off;
    size_t            cand_count_off;
    size_t            candidates_off;
    size_t            transitions_off;

    if (dfa == NULL || !dfa->present) {
        return PROTECC_OK;
    }

    required = __net_dfa_block_size(dfa);
    if ((size_t)offset > bufferSize || required > bufferSize - offset) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    classmap_off = sizeof(protecc_profile_dfa_t);
    accept_off = classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    cand_index_off = accept_off + ((size_t)dfa->accept_words * sizeof(uint32_t));
    cand_count_off = cand_index_off + ((size_t)dfa->num_states * sizeof(uint32_t));
    candidates_off = cand_count_off + ((size_t)dfa->num_states * sizeof(uint32_t));
    transitions_off = candidates_off + ((size_t)dfa->candidates_total * sizeof(uint32_t));

    if (classmap_off > UINT32_MAX || accept_off > UINT32_MAX || cand_index_off > UINT32_MAX
        || cand_count_off > UINT32_MAX || candidates_off > UINT32_MAX || transitions_off > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.num_states = dfa->num_states;
    header.num_classes = dfa->num_classes;
    header.start_state = dfa->start_state;
    header.accept_words = dfa->accept_words;
    header.classmap_off = (uint32_t)classmap_off;
    header.accept_off = (uint32_t)accept_off;
    header.candidate_index_off = (uint32_t)cand_index_off;
    header.candidate_count_off = (uint32_t)cand_count_off;
    header.candidates_off = (uint32_t)candidates_off;
    header.candidates_count = dfa->candidates_total;
    header.transitions_off = (uint32_t)transitions_off;

    out = base + offset;
    memcpy(out, &header, sizeof(header));
    memcpy(out + classmap_off, dfa->classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(out + accept_off, dfa->accept, (size_t)dfa->accept_words * sizeof(uint32_t));
    memcpy(out + cand_index_off, dfa->candidate_index, (size_t)dfa->num_states * sizeof(uint32_t));
    memcpy(out + cand_count_off, dfa->candidate_count, (size_t)dfa->num_states * sizeof(uint32_t));
    memcpy(out + candidates_off, dfa->candidates, (size_t)dfa->candidates_total * sizeof(uint32_t));
    memcpy(
        out + transitions_off,
        dfa->transitions,
        (size_t)dfa->num_states * (size_t)dfa->num_classes * sizeof(uint32_t)
    );
    return PROTECC_OK;
}

protecc_error_t __net_validate_dfa_block(
    const uint8_t* blockBase,
    size_t         bufferSize,
    size_t         blockOffset,
    size_t         ruleCount,
    size_t*        blockSizeOut)
{
    protecc_profile_dfa_t header;
    size_t            classmap_off;
    size_t            accept_off;
    size_t            cand_index_off;
    size_t            cand_count_off;
    size_t            candidates_off;
    size_t            transitions_off;
    size_t            required;
    size_t            transitions_count;
    uint64_t          tmp;

    if (blockOffset > bufferSize || bufferSize - blockOffset < sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    memcpy(&header, blockBase + blockOffset, sizeof(header));

    if (header.num_states == 0 || header.num_classes == 0 || header.num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.accept_words != ((header.num_states + 31u) / 32u)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    classmap_off = header.classmap_off;
    accept_off = header.accept_off;
    cand_index_off = header.candidate_index_off;
    cand_count_off = header.candidate_count_off;
    candidates_off = header.candidates_off;
    transitions_off = header.transitions_off;

    required = sizeof(protecc_profile_dfa_t)
        + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        + ((size_t)header.accept_words * sizeof(uint32_t))
        + ((size_t)header.num_states * sizeof(uint32_t))
        + ((size_t)header.num_states * sizeof(uint32_t))
        + ((size_t)header.candidates_count * sizeof(uint32_t));

    tmp = (uint64_t)header.num_states * (uint64_t)header.num_classes;
    if (tmp > SIZE_MAX / sizeof(uint32_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }
    transitions_count = (size_t)tmp;
    if (transitions_off < candidates_off + ((size_t)header.candidates_count * sizeof(uint32_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    required = transitions_off + (transitions_count * sizeof(uint32_t));

    if (required > bufferSize - blockOffset) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (classmap_off < sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (accept_off < classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        || cand_index_off < accept_off + ((size_t)header.accept_words * sizeof(uint32_t))
        || cand_count_off < cand_index_off + ((size_t)header.num_states * sizeof(uint32_t))
        || candidates_off < cand_count_off + ((size_t)header.num_states * sizeof(uint32_t))
        || transitions_off < candidates_off + ((size_t)header.candidates_count * sizeof(uint32_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    /* Verify candidates do not exceed declared total and rule bounds */
    {
        const uint32_t* counts = (const uint32_t*)(blockBase + blockOffset + cand_count_off);
        const uint32_t* starts = (const uint32_t*)(blockBase + blockOffset + cand_index_off);
        size_t          total = 0;

        for (uint32_t i = 0; i < header.num_states; i++) {
            uint32_t count = counts[i];
            uint32_t start = starts[i];

            if (count > ruleCount) {
                return PROTECC_ERROR_INVALID_BLOB;
            }

            if ((uint64_t)start + (uint64_t)count > header.candidates_count) {
                return PROTECC_ERROR_INVALID_BLOB;
            }

            total += count;
        }

        if (total != header.candidates_count) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }

    /* Verify transitions stay within state range */
    {
        const uint32_t* transitions = (const uint32_t*)(blockBase + blockOffset + transitions_off);

        for (size_t i = 0; i < transitions_count; i++) {
            if (transitions[i] >= header.num_states) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }
    }

    if (blockSizeOut) {
        *blockSizeOut = required;
    }

    return PROTECC_OK;
}

typedef struct {
    uint32_t ip_pattern_off;
    uint32_t unix_path_pattern_off;
} protecc_net_rule_offsets_t;

static protecc_error_t __export_net_rule(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   stringsSize,
    const protecc_net_rule_offsets_t* offsets)
{
    uint8_t*                    out = (uint8_t*)buffer;
    protecc_net_profile_rule_t* rules =
        (protecc_net_profile_rule_t*)(out + sizeof(protecc_rule_profile_header_t));
    uint8_t*                    strings =
        out + sizeof(protecc_rule_profile_header_t)
        + (profile->net_rule_count * sizeof(protecc_net_profile_rule_t));
    size_t cursor = 0;

    if (offsets == NULL && profile->net_rule_count != 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(rules, 0, profile->net_rule_count * sizeof(protecc_net_profile_rule_t));

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        uint32_t ip_pattern_off;
        uint32_t unix_pattern_off;

        rules[i].action = (uint8_t)profile->net_rules[i].action;
        rules[i].protocol = (uint8_t)profile->net_rules[i].protocol;
        rules[i].family = (uint8_t)profile->net_rules[i].family;
        rules[i].port_from = profile->net_rules[i].port_from;
        rules[i].port_to = profile->net_rules[i].port_to;

        ip_pattern_off = __blob_string_write(strings, &cursor, profile->net_rules[i].ip_pattern);
        unix_pattern_off = __blob_string_write(strings, &cursor, profile->net_rules[i].unix_path_pattern);

        if (ip_pattern_off != offsets[i].ip_pattern_off
            || unix_pattern_off != offsets[i].unix_path_pattern_off) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }

        rules[i].ip_pattern_off = ip_pattern_off;
        rules[i].unix_path_pattern_off = unix_pattern_off;
    }

    if (cursor != stringsSize) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }
    return PROTECC_OK;
}

static protecc_error_t __export_net_profile(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    protecc_rule_profile_header_t header;
    protecc_net_rule_offsets_t*  offsets = NULL;
    protecc_charclass_table_t    charclasses = {0};
    size_t                       stringsSize = 0;
    size_t                       requiredSize;
    size_t                       rulesSize;
    size_t                       classTableSize;
    size_t                       ipDfaSize;
    size_t                       unixDfaSize;
    size_t                       dfaSectionSize = 0;
    size_t                       dfaSectionOff = 0;
    bool                         caseInsensitive;
    protecc_error_t              err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    caseInsensitive = (profile->flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;
    rulesSize = profile->net_rule_count * sizeof(protecc_net_profile_rule_t);

    if (profile->net_rule_count > PROTECC_MAX_RULES) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (profile->net_rule_count > 0) {
        offsets = calloc(profile->net_rule_count, sizeof(protecc_net_rule_offsets_t));
        if (!offsets) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        uint32_t ip_offset = PROTECC_PROFILE_STRING_NONE;
        uint32_t unix_offset = PROTECC_PROFILE_STRING_NONE;
        size_t   measurement;
        
        measurement = __blob_string_measure(profile->net_rules[i].ip_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            ip_offset = (uint32_t)stringsSize;
            err = __charclass_collect(profile->net_rules[i].ip_pattern,
                                      ip_offset,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        measurement = __blob_string_measure(profile->net_rules[i].unix_path_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            unix_offset = (uint32_t)stringsSize;
            err = __charclass_collect(profile->net_rules[i].unix_path_pattern,
                                      unix_offset,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        offsets[i].ip_pattern_off = ip_offset;
        offsets[i].unix_path_pattern_off = unix_offset;
    }

    if (stringsSize > UINT32_MAX || charclasses.count > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    classTableSize = charclasses.count * sizeof(protecc_profile_charclass_entry_t);
    if (classTableSize > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    ipDfaSize = __net_dfa_block_size(profile->net_ip_dfa);
    unixDfaSize = __net_dfa_block_size(profile->net_unix_dfa);

    if (profile->net_rule_count > 0 && ipDfaSize == 0 && unixDfaSize == 0) {
        err = PROTECC_ERROR_COMPILE_FAILED;
        goto cleanup;
    }

    if (ipDfaSize > 0 || unixDfaSize > 0) {
        dfaSectionSize = sizeof(protecc_net_dfa_section_t) + ipDfaSize + unixDfaSize;
        if (dfaSectionSize > UINT32_MAX) {
            err = PROTECC_ERROR_INVALID_ARGUMENT;
            goto cleanup;
        }
    }

    requiredSize = sizeof(protecc_rule_profile_header_t)
        + rulesSize
        + stringsSize
        + classTableSize
        + dfaSectionSize;

    if (requiredSize > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (!buffer) {
        err = PROTECC_OK;
        goto cleanup;
    }

    if (bufferSize < requiredSize) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_NET_PROFILE_MAGIC;
    header.version = PROTECC_NET_PROFILE_VERSION;
    header.flags = caseInsensitive ? PROTECC_PROFILE_FLAG_CASE_INSENSITIVE : 0u;
    header.rule_count = (uint32_t)profile->net_rule_count;
    header.strings_size = (uint32_t)stringsSize;
    header.charclass_count = (uint32_t)charclasses.count;
    header.charclass_table_off = (uint32_t)(sizeof(protecc_rule_profile_header_t) + rulesSize + stringsSize);
    if (dfaSectionSize > 0) {
        header.dfa_section_off = (uint32_t)(header.charclass_table_off + classTableSize);
        dfaSectionOff = header.dfa_section_off;
    } else {
        header.dfa_section_off = 0;
    }

    memcpy(buffer, &header, sizeof(header));

    err = __export_net_rule(profile, buffer, stringsSize, offsets);
    if (err != PROTECC_OK) {
        goto cleanup;
    }

    if (charclasses.count > 0) {
        protecc_profile_charclass_entry_t* out_classes;

        out_classes = (protecc_profile_charclass_entry_t*)((uint8_t*)buffer + header.charclass_table_off);
        memcpy(out_classes, charclasses.entries, charclasses.count * sizeof(protecc_profile_charclass_entry_t));
    }

    if (dfaSectionSize > 0) {
        protecc_net_dfa_section_t* section;
        uint8_t*                   out_base = (uint8_t*)buffer;
        uint32_t                   cursor = sizeof(protecc_net_dfa_section_t);

        section = (protecc_net_dfa_section_t*)(out_base + dfaSectionOff);
        memset(section, 0, sizeof(*section));

        if (ipDfaSize > 0) {
            section->ip_dfa_off = cursor;
            err = __net_export_dfa_block(profile->net_ip_dfa, out_base + dfaSectionOff, bufferSize - dfaSectionOff, cursor);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            cursor += (uint32_t)ipDfaSize;
        }

        if (unixDfaSize > 0) {
            section->unix_dfa_off = cursor;
            err = __net_export_dfa_block(profile->net_unix_dfa, out_base + dfaSectionOff, bufferSize - dfaSectionOff, cursor);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            cursor += (uint32_t)unixDfaSize;
        }
    }

    err = PROTECC_OK;

cleanup:
    __charclass_table_free(&charclasses);
    free(offsets);
    return err;
}

protecc_error_t protecc_profile_export_net(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    return __export_net_profile(profile, buffer, bufferSize, bytesWritten);
}

protecc_error_t protecc_profile_import_net_blob(
    const void*          buffer,
    size_t               bufferSize,
    protecc_net_rule_t** rulesOut,
    size_t*              countOut)
{
    const uint8_t*                    base;
    protecc_rule_profile_header_t      header;
    const protecc_net_profile_rule_t* inRules;
    const uint8_t*                    strings;
    protecc_net_rule_t*               outRules;
    size_t                            rulesSize;
    protecc_error_t                   err;

    if (buffer == NULL || rulesOut == NULL || countOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.rule_count == 0) {
        return PROTECC_OK;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    inRules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;

    outRules = calloc(header.rule_count, sizeof(protecc_net_rule_t));
    if (!outRules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        outRules[i].action = (protecc_action_t)inRules[i].action;
        outRules[i].protocol = (protecc_net_protocol_t)inRules[i].protocol;
        outRules[i].family = (protecc_net_family_t)inRules[i].family;
        outRules[i].port_from = inRules[i].port_from;
        outRules[i].port_to = inRules[i].port_to;

        outRules[i].ip_pattern = __blob_string_dup(strings, inRules[i].ip_pattern_off);
        outRules[i].unix_path_pattern = __blob_string_dup(strings, inRules[i].unix_path_pattern_off);

        if ((inRules[i].ip_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].ip_pattern)
            || (inRules[i].unix_path_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].unix_path_pattern)) {
            protecc_profile_free_net_rules(outRules, i + 1u);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    *rulesOut = outRules;
    *countOut = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_validate_net_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t*                    base;
    protecc_rule_profile_header_t      header;
    size_t                            rulesSize;
    size_t                            required;
    size_t                            classTableSize;
    const protecc_net_profile_rule_t* rules;
    const uint8_t*                    strings;
    const protecc_profile_charclass_entry_t* classes;
    protecc_error_t                   err;

    if (buffer == NULL || bufferSize < sizeof(protecc_rule_profile_header_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_NET_PROFILE_MAGIC || header.version != PROTECC_NET_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.rule_count > PROTECC_MAX_RULES) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if ((header.flags & ~(PROTECC_PROFILE_FLAG_CASE_INSENSITIVE)) != 0) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.charclass_count > PROTECC_PROFILE_MAX_CHAR_CLASSES) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    classTableSize = (size_t)header.charclass_count * sizeof(protecc_profile_charclass_entry_t);

    if (header.charclass_table_off < sizeof(protecc_rule_profile_header_t) + rulesSize + (size_t)header.strings_size) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if ((size_t)header.charclass_table_off > SIZE_MAX - classTableSize) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    required = (size_t)header.charclass_table_off + classTableSize;

    if (required < sizeof(protecc_rule_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;
    classes = (const protecc_profile_charclass_entry_t*)(base + header.charclass_table_off);

    if (classTableSize > 0) {
        if (classes < (const protecc_profile_charclass_entry_t*)base
            || (const uint8_t*)(classes + header.charclass_count) > (base + bufferSize)) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t       action = (protecc_action_t)rules[i].action;
        protecc_net_protocol_t protocol = (protecc_net_protocol_t)rules[i].protocol;
        protecc_net_family_t   family = (protecc_net_family_t)rules[i].family;
        protecc_error_t        err;

        if (!__is_valid_action(action)
            || !__is_valid_net_protocol(protocol)
            || !__is_valid_net_family(family)
            || rules[i].port_from > rules[i].port_to) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (protocol == PROTECC_NET_PROTOCOL_UNIX) {
            if (family == PROTECC_NET_FAMILY_IPV4 || family == PROTECC_NET_FAMILY_IPV6) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
            if (rules[i].port_from != 0 || rules[i].port_to != 0) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        if (family == PROTECC_NET_FAMILY_UNIX) {
            if (protocol == PROTECC_NET_PROTOCOL_TCP || protocol == PROTECC_NET_PROTOCOL_UDP) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        err = __validate_blob_string_offset(rules[i].ip_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].ip_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].ip_pattern_off), header.strings_size - rules[i].ip_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        err = __validate_blob_string_offset(rules[i].unix_path_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].unix_path_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].unix_path_pattern_off), header.strings_size - rules[i].unix_path_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }
    }

    for (uint32_t i = 0; i < header.charclass_count; i++) {
        uint64_t end;

        if (classes[i].consumed == 0) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        end = (uint64_t)classes[i].pattern_off + (uint64_t)classes[i].consumed;
        if (end > header.strings_size) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }

    if (header.dfa_section_off != 0) {
        const protecc_net_dfa_section_t* section;
        size_t                           section_end;

        if (header.dfa_section_off < header.charclass_table_off + classTableSize) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (header.dfa_section_off > bufferSize - sizeof(protecc_net_dfa_section_t)) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        section = (const protecc_net_dfa_section_t*)(base + header.dfa_section_off);
        section_end = header.dfa_section_off + sizeof(*section);

        if (section->ip_dfa_off != 0) {
            size_t ip_size = 0;
            size_t ip_block_off;

            if (section->ip_dfa_off > SIZE_MAX - header.dfa_section_off) {
                return PROTECC_ERROR_INVALID_BLOB;
            }

            err = __net_validate_dfa_block(
                base,
                bufferSize,
                header.dfa_section_off + section->ip_dfa_off,
                header.rule_count,
                &ip_size);
            if (err != PROTECC_OK) {
                return err;
            }

            ip_block_off = header.dfa_section_off + section->ip_dfa_off;
            if (section_end < ip_block_off + ip_size) {
                section_end = ip_block_off + ip_size;
            }
        }

        if (section->unix_dfa_off != 0) {
            size_t unix_size = 0;
            size_t unix_block_off;

            if (section->unix_dfa_off > SIZE_MAX - header.dfa_section_off) {
                return PROTECC_ERROR_INVALID_BLOB;
            }

            err = __net_validate_dfa_block(
                base,
                bufferSize,
                header.dfa_section_off + section->unix_dfa_off,
                header.rule_count,
                &unix_size);
            if (err != PROTECC_OK) {
                return err;
            }

            unix_block_off = header.dfa_section_off + section->unix_dfa_off;
            if (section_end < unix_block_off + unix_size) {
                section_end = unix_block_off + unix_size;
            }
        }

        if (section_end > bufferSize) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    } else if (header.rule_count > 0) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_validate_mount_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t*                      base;
    protecc_rule_profile_header_t      header;
    size_t                              rulesSize;
    size_t                              required;
    size_t                              dfa_size = 0;
    size_t                              classTableSize;
    const protecc_mount_profile_rule_t* rules;
    const uint8_t*                      strings;
    const protecc_profile_charclass_entry_t* classes;

    if (buffer == NULL || bufferSize < sizeof(protecc_rule_profile_header_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_MOUNT_PROFILE_MAGIC || header.version != PROTECC_MOUNT_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if ((header.flags & ~(PROTECC_PROFILE_FLAG_CASE_INSENSITIVE)) != 0) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.rule_count > (SIZE_MAX / sizeof(protecc_mount_profile_rule_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.charclass_count > PROTECC_PROFILE_MAX_CHAR_CLASSES) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    classTableSize = (size_t)header.charclass_count * sizeof(protecc_profile_charclass_entry_t);

    if (header.charclass_table_off < sizeof(protecc_rule_profile_header_t) + rulesSize + (size_t)header.strings_size) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if ((size_t)header.charclass_table_off > SIZE_MAX - classTableSize) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    required = (size_t)header.charclass_table_off + classTableSize;

    if (header.rule_count > 0) {
        if (header.dfa_section_off == 0) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (header.dfa_section_off < required) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (__net_validate_dfa_block(base, bufferSize, header.dfa_section_off, header.rule_count, &dfa_size) != PROTECC_OK) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (header.dfa_section_off > SIZE_MAX - dfa_size) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (required < header.dfa_section_off + dfa_size) {
            required = header.dfa_section_off + dfa_size;
        }
    }

    if (required < sizeof(protecc_rule_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;
    classes = (const protecc_profile_charclass_entry_t*)(base + header.charclass_table_off);

    if (classTableSize > 0) {
        if (classes < (const protecc_profile_charclass_entry_t*)base
            || (const uint8_t*)(classes + header.charclass_count) > (base + bufferSize)) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t action = (protecc_action_t)rules[i].action;
        protecc_error_t  err;

        if (!__is_valid_action(action)) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        err = __validate_blob_string_offset(rules[i].source_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].source_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].source_pattern_off), header.strings_size - rules[i].source_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        err = __validate_blob_string_offset(rules[i].target_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].target_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].target_pattern_off), header.strings_size - rules[i].target_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }
        
        err = __validate_blob_string_offset(rules[i].fstype_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].fstype_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].fstype_pattern_off), header.strings_size - rules[i].fstype_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }
        
        err = __validate_blob_string_offset(rules[i].options_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        if (rules[i].options_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            size_t len = strnlen((const char*)(strings + rules[i].options_pattern_off), header.strings_size - rules[i].options_pattern_off);
            if (len > PROTECC_MAX_GLOB_STEPS) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }
    }

    for (uint32_t i = 0; i < header.charclass_count; i++) {
        uint64_t end;

        if (classes[i].consumed == 0) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        end = (uint64_t)classes[i].pattern_off + (uint64_t)classes[i].consumed;
        if (end > header.strings_size) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_init(
    const void*              buffer,
    size_t                   bufferSize,
    protecc_net_blob_view_t* viewOut)
{
    protecc_rule_profile_header_t header;
    protecc_error_t              err;

    if (!buffer || !viewOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(&header, buffer, sizeof(header));

    viewOut->blob = buffer;
    viewOut->blob_size = bufferSize;
    viewOut->rule_count = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_get_rule(
    const protecc_net_blob_view_t* view,
    size_t                         index,
    protecc_net_rule_view_t*       ruleOut)
{
    const uint8_t*                    base;
    protecc_rule_profile_header_t      header;
    size_t                            rulesSize;
    const protecc_net_profile_rule_t* rules;
    const uint8_t*                    strings;
    const protecc_net_profile_rule_t* inRule;
    protecc_error_t                   err;

    if (view == NULL || ruleOut == NULL || view->blob == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(view->blob, view->blob_size);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)view->blob;
    memcpy(&header, base, sizeof(header));

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;
    inRule = &rules[index];

    ruleOut->action = (protecc_action_t)inRule->action;
    ruleOut->protocol = (protecc_net_protocol_t)inRule->protocol;
    ruleOut->family = (protecc_net_family_t)inRule->family;
    ruleOut->port_from = inRule->port_from;
    ruleOut->port_to = inRule->port_to;
    ruleOut->ip_pattern = __blob_string_ptr(strings, inRule->ip_pattern_off);
    ruleOut->unix_path_pattern = __blob_string_ptr(strings, inRule->unix_path_pattern_off);
    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_first(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut)
{
    if (iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (view == NULL || view->rule_count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = 0;
    return protecc_profile_net_view_get_rule(view, 0, ruleOut);
}

protecc_error_t protecc_profile_net_view_next(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut)
{
    size_t nextIndex;

    if (view == NULL || iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nextIndex = *iterIndexInOut + 1u;
    if (nextIndex >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = nextIndex;
    return protecc_profile_net_view_get_rule(view, nextIndex, ruleOut);
}

protecc_error_t __validate_net_rule(const protecc_net_rule_t* rule)
{
    if (!__is_valid_action(rule->action)
        || !__is_valid_net_protocol(rule->protocol)
        || !__is_valid_net_family(rule->family)
        || rule->port_from > rule->port_to) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (rule->protocol == PROTECC_NET_PROTOCOL_UNIX) {
        if (rule->family == PROTECC_NET_FAMILY_IPV4 || rule->family == PROTECC_NET_FAMILY_IPV6) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
        if (rule->port_from != 0 || rule->port_to != 0) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    if (rule->family == PROTECC_NET_FAMILY_UNIX) {
        if (rule->protocol == PROTECC_NET_PROTOCOL_TCP || rule->protocol == PROTECC_NET_PROTOCOL_UDP) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    if (rule->unix_path_pattern) {
        protecc_error_t path_err = protecc_validate_pattern(rule->unix_path_pattern);
        if (path_err != PROTECC_OK) {
            return path_err;
        }
    }

    return PROTECC_OK;
}

void protecc_profile_free_net_rules(
    protecc_net_rule_t* rules,
    size_t              count)
{
    if (rules == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free((void*)rules[i].ip_pattern);
        free((void*)rules[i].unix_path_pattern);
    }

    free(rules);
}

void __net_dfa_free_runtime(protecc_rule_dfa_runtime_t* dfa)
{
    if (dfa == NULL) {
        return;
    }

    free(dfa->accept);
    free(dfa->candidate_index);
    free(dfa->candidate_count);
    free(dfa->candidates);
    free(dfa->transitions);
    free(dfa);
}

protecc_error_t __protecc_net_build_dfa(protecc_profile_t* profile)
{
    protecc_rule_dfa_pattern_t ip_patterns[PROTECC_MAX_RULES];
    protecc_rule_dfa_pattern_t unix_patterns[PROTECC_MAX_RULES];
    size_t              ip_count = 0;
    size_t              unix_count = 0;
    protecc_error_t     err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    __net_dfa_free_runtime(profile->net_ip_dfa);
    __net_dfa_free_runtime(profile->net_unix_dfa);
    profile->net_ip_dfa = NULL;
    profile->net_unix_dfa = NULL;

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        const protecc_net_rule_t* rule = &profile->net_rules[i];
        const char*               pattern = NULL;

        if (rule->protocol == PROTECC_NET_PROTOCOL_UNIX || rule->family == PROTECC_NET_FAMILY_UNIX) {
            pattern = rule->unix_path_pattern ? rule->unix_path_pattern : "*";
            if (unix_count >= PROTECC_MAX_RULES) {
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            unix_patterns[unix_count].pattern = pattern;
            unix_patterns[unix_count].rule_index = (uint32_t)i;
            unix_count++;
        } else {
            pattern = rule->ip_pattern ? rule->ip_pattern : "*";
            if (ip_count >= PROTECC_MAX_RULES) {
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            ip_patterns[ip_count].pattern = pattern;
            ip_patterns[ip_count].rule_index = (uint32_t)i;
            ip_count++;
        }
    }

    err = __build_dfa_from_patterns(ip_patterns, ip_count, profile, &profile->net_ip_dfa);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __build_dfa_from_patterns(unix_patterns, unix_count, profile, &profile->net_unix_dfa);
    if (err != PROTECC_OK) {
        __net_dfa_free_runtime(profile->net_ip_dfa);
        profile->net_ip_dfa = NULL;
        return err;
    }

    return PROTECC_OK;
}

void __protecc_net_free_dfas(protecc_profile_t* profile)
{
    if (profile == NULL) {
        return;
    }

    __net_dfa_free_runtime(profile->net_ip_dfa);
    __net_dfa_free_runtime(profile->net_unix_dfa);
    profile->net_ip_dfa = NULL;
    profile->net_unix_dfa = NULL;
}
