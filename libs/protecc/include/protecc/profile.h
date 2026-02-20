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

#ifndef __PROTECC_PROFILE_H__
#define __PROTECC_PROFILE_H__

#ifndef __VMLINUX_H__
#include <stdint.h>
#endif

#ifdef _MSC_VER
#define PROTECC_ONDISK_STRUCT(name, body) __pragma(pack(push, 1)) typedef struct name body name##_t __pragma(pack(pop))
#else
#define PROTECC_ONDISK_STRUCT(name, body) typedef struct __attribute__((packed)) name body name##_t
#endif

/**
 * @brief On-disk format for compiled profiles. This is what the userspace 
 * compiler emits and what the BPF program reads. The format is designed for 
 * efficient loading and matching within BPF, with a header containing metadata and a 
 * compact representation of the pattern matching automaton as a flat array of nodes and edges.
 */
#define PROTECC_PROFILE_MAGIC   0x50524F54u // "PROT"
#define PROTECC_PROFILE_VERSION 0x00010001u

#define PROTECC_NET_PROFILE_MAGIC   0x50524E54u // "PRNT"
#define PROTECC_NET_PROFILE_VERSION 0x00010000u

#define PROTECC_MOUNT_PROFILE_MAGIC   0x50524D54u // "PRMT"
#define PROTECC_MOUNT_PROFILE_VERSION 0x00010000u

#define PROTECC_PROFILE_STRING_NONE 0xFFFFFFFFu

#define PROTECC_PROFILE_FLAG_CASE_INSENSITIVE (1u << 0)
#define PROTECC_PROFILE_FLAG_OPTIMIZE         (1u << 1)
#define PROTECC_PROFILE_FLAG_TYPE_TRIE        (1u << 8)
#define PROTECC_PROFILE_FLAG_TYPE_DFA         (1u << 9)

#define PROTECC_PROFILE_DFA_CLASSMAP_SIZE 256u

PROTECC_ONDISK_STRUCT(protecc_profile_stats, {
    uint32_t num_patterns;
    uint32_t binary_size;
    uint32_t max_depth;
    uint32_t num_nodes;
});

PROTECC_ONDISK_STRUCT(protecc_profile_header, {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t num_nodes;
    uint32_t num_edges;
    uint32_t root_index;
    protecc_profile_stats_t stats;
});

PROTECC_ONDISK_STRUCT(protecc_profile_dfa, {
    uint32_t num_states;
    uint32_t num_classes;
    uint32_t start_state;
    uint32_t accept_words;
    uint32_t classmap_off;
    uint32_t accept_off;
    uint32_t perms_off;
    uint32_t transitions_off;
});

PROTECC_ONDISK_STRUCT(protecc_profile_node, {
    uint8_t type;
    uint8_t modifier;
    uint8_t is_terminal;
    uint8_t reserved;
    uint32_t child_start;
    uint16_t child_count;
    uint16_t reserved2;
    uint32_t perms;
    union {
        uint8_t literal;
        struct {
            uint8_t start;
            uint8_t end;
            uint8_t pad[30];
        } range;
        uint8_t charset[32];
    } data;
});

PROTECC_ONDISK_STRUCT(protecc_net_profile_header, {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t rule_count;
    uint32_t strings_size;
    uint32_t reserved[3];
});

PROTECC_ONDISK_STRUCT(protecc_net_profile_rule, {
    uint8_t  action;
    uint8_t  protocol;
    uint8_t  family;
    uint8_t  reserved;
    uint16_t port_from;
    uint16_t port_to;
    uint32_t ip_pattern_off;
    uint32_t unix_path_pattern_off;
});

PROTECC_ONDISK_STRUCT(protecc_mount_profile_header, {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t rule_count;
    uint32_t strings_size;
    uint32_t reserved[3];
});

PROTECC_ONDISK_STRUCT(protecc_mount_profile_rule, {
    uint8_t  action;
    uint8_t  reserved[3];
    uint32_t flags;
    uint32_t source_pattern_off;
    uint32_t target_pattern_off;
    uint32_t fstype_pattern_off;
    uint32_t options_pattern_off;
});

#endif // !__PROTECC_PROFILE_H__
