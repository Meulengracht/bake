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
 * Protecc compiles path patterns with wildcards and simple regex into an
 * optimized binary format (trie-based) for fast evaluation in eBPF programs.
 * 
 * Supported patterns:
 * - ? : matches any single character
 * - * : matches any sequence of characters (except /)
 * - ** : matches any sequence including / (recursive directory match)
 * - [a-z] : character range
 * - [0-9] : digit range
 * - [abc] : character set
 * - Modifiers: ? (0 or 1), + (1 or more), * (0 or more)
 * 
 * Example patterns:
 * - /etc/passwd : exact match
 * - /tmp/* : matches any file in /tmp
 * - /home/** : matches anything under /home recursively
 * - /var/log/[a-z]*.log : matches log files starting with letter
 * - /dev/tty[0-9]+ : matches /dev/tty0, /dev/tty1, etc.
 */

#ifndef __PROTECC_H__
#define __PROTECC_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Opaque handle to a compiled profile
 */
typedef struct protecc_profile protecc_profile_t;
typedef struct protecc_profile_builder protecc_profile_builder_t;

/**
 * @brief Pattern compilation flags
 */
typedef enum {
    PROTECC_FLAG_NONE = 0,
    PROTECC_FLAG_CASE_INSENSITIVE = 1 << 0,  /**< Case-insensitive matching */
    PROTECC_FLAG_OPTIMIZE = 1 << 1,           /**< Enable optimizations (default) */
} protecc_flags_t;

/**
 * @brief Compilation backend mode
 */
typedef enum {
    PROTECC_COMPILE_MODE_TRIE = 0, /**< Compile patterns to trie profile (default backend) */
    PROTECC_COMPILE_MODE_DFA = 1,  /**< Compile patterns to DFA profile */
} protecc_compile_mode_t;

/**
 * @brief Compiler limits and backend selection
 *
 * All max_* values are hard upper bounds enforced during compile:
 * - max_patterns: maximum number of patterns accepted in one compile operation.
 * - max_pattern_length: maximum length in bytes for any single pattern string (excluding NUL).
 * - max_states: maximum automaton states allowed for selected backend.
 *               For trie mode this limits number of trie nodes.
 * - max_classes: maximum character classes allowed for DFA alphabet compression.
 *                Unused in trie mode, but still validated (>0) for forward compatibility.
 */
typedef struct {
    uint32_t mode;
    uint32_t max_patterns;
    uint32_t max_pattern_length;
    uint32_t max_states;
    uint32_t max_classes;
} protecc_compile_config_t;

/**
 * @brief Error codes
 */
typedef enum {
    PROTECC_OK = 0,
    PROTECC_ERROR_INVALID_PATTERN = -1,
    PROTECC_ERROR_OUT_OF_MEMORY = -2,
    PROTECC_ERROR_INVALID_ARGUMENT = -3,
    PROTECC_ERROR_COMPILE_FAILED = -4,
    PROTECC_ERROR_NOT_SUPPORTED = -5,
    PROTECC_ERROR_INVALID_BLOB = -6,
} protecc_error_t;

/**
 * @brief High-level profile action for future multi-domain policy engines
 */
typedef enum {
    PROTECC_ACTION_ALLOW = 0,
    PROTECC_ACTION_DENY = 1,
    PROTECC_ACTION_AUDIT = 2,
} protecc_action_t;

/**
 * @brief Network protocol selector for network rules
 */
typedef enum {
    PROTECC_NET_PROTOCOL_ANY = 0,
    PROTECC_NET_PROTOCOL_TCP = 1,
    PROTECC_NET_PROTOCOL_UDP = 2,
    PROTECC_NET_PROTOCOL_UNIX = 3,
} protecc_net_protocol_t;

/**
 * @brief Network address family selector
 */
typedef enum {
    PROTECC_NET_FAMILY_ANY = 0,
    PROTECC_NET_FAMILY_IPV4 = 1,
    PROTECC_NET_FAMILY_IPV6 = 2,
    PROTECC_NET_FAMILY_UNIX = 3,
} protecc_net_family_t;

/**
 * @brief Rule describing network mediation intent
 */
typedef struct {
    protecc_action_t       action;
    protecc_net_protocol_t protocol;
    protecc_net_family_t   family;
    const char*            ip_pattern;
    uint16_t               port_from;
    uint16_t               port_to;
    const char*            unix_path_pattern;
} protecc_net_rule_t;

/**
 * @brief Rule describing mount mediation intent
 */
typedef struct {
    protecc_action_t action;
    const char*      source_pattern;
    const char*      target_pattern;
    const char*      fstype_pattern;
    const char*      options_pattern;
    uint32_t         flags;
} protecc_mount_rule_t;

/**
 * @brief Permission flags for compiled patterns
 */
typedef enum {
    PROTECC_PERM_NONE = 0,
    PROTECC_PERM_READ = 1 << 0,
    PROTECC_PERM_WRITE = 1 << 1,
    PROTECC_PERM_EXECUTE = 1 << 2,
    PROTECC_PERM_ALL = PROTECC_PERM_READ | PROTECC_PERM_WRITE | PROTECC_PERM_EXECUTE,
} protecc_permission_t;

/**
 * @brief Statistics about compiled patterns
 */
typedef struct {
    size_t num_patterns;      /**< Number of patterns compiled */
    size_t binary_size;       /**< Size of compiled binary in bytes */
    size_t max_depth;         /**< Maximum trie depth */
    size_t num_nodes;         /**< Number of trie nodes */
} protecc_stats_t;

/**
 * @brief Represents a pattern and its associated permissions
 */
typedef struct {
    const char*          pattern;   /**< Original pattern string */
    protecc_permission_t perms;     /**< Permissions associated with this pattern */
} protecc_pattern_t;

/**
 * @brief Zero-copy view over a net profile blob
 */
typedef struct {
    const void* blob;
    size_t      blob_size;
    size_t      rule_count;
} protecc_net_blob_view_t;

/**
 * @brief Zero-copy decoded net rule
 */
typedef struct {
    protecc_action_t       action;
    protecc_net_protocol_t protocol;
    protecc_net_family_t   family;
    uint16_t               port_from;
    uint16_t               port_to;
    const char*            ip_pattern;
    const char*            unix_path_pattern;
} protecc_net_rule_view_t;

/**
 * @brief Zero-copy view over a mount profile blob
 */
typedef struct {
    const void* blob;
    size_t      blob_size;
    size_t      rule_count;
} protecc_mount_blob_view_t;

/**
 * @brief Zero-copy decoded mount rule
 */
typedef struct {
    protecc_action_t action;
    uint32_t         flags;
    const char*      source_pattern;
    const char*      target_pattern;
    const char*      fstype_pattern;
    const char*      options_pattern;
} protecc_mount_rule_view_t;

/**
 * @brief Runtime network access request for rule matching
 */
typedef struct {
    protecc_net_protocol_t protocol;
    protecc_net_family_t   family;
    const char*            ip;
    uint16_t               port;
    const char*            unix_path;
} protecc_net_request_t;

/**
 * @brief Runtime mount request for rule matching
 */
typedef struct {
    const char* source;
    const char* target;
    const char* fstype;
    const char* options;
    uint32_t    flags;
} protecc_mount_request_t;

/**
 * @brief Initialize compiler config with defaults
 *
 * Defaults:
 * - mode = PROTECC_COMPILE_MODE_TRIE
 * - max_patterns = 256
 * - max_pattern_length = 128
 * - max_states = 2048
 * - max_classes = 32
 */
void protecc_compile_config_default(protecc_compile_config_t* config);

/**
 * @brief Create a new profile builder
 */
protecc_profile_builder_t* protecc_profile_builder_create(void);

/**
 * @brief Reset a profile builder and remove all queued rules
 */
void protecc_profile_builder_reset(protecc_profile_builder_t* builder);

/**
 * @brief Destroy a profile builder
 */
void protecc_profile_builder_destroy(protecc_profile_builder_t* builder);

/**
 * @brief Add path patterns to builder
 */
protecc_error_t protecc_profile_builder_add_patterns(
    protecc_profile_builder_t* builder,
    const protecc_pattern_t*   patterns,
    size_t                     count);

/**
 * @brief Add a network rule to builder
 */
protecc_error_t protecc_profile_builder_add_net_rule(
    protecc_profile_builder_t* builder,
    const protecc_net_rule_t*  rule);

/**
 * @brief Add a mount rule to builder
 */
protecc_error_t protecc_profile_builder_add_mount_rule(
    protecc_profile_builder_t*   builder,
    const protecc_mount_rule_t*  rule);

/**
 * @brief Add a mount rule to builder (alias for add_mount_rule)
 */
protecc_error_t protecc_profile_builder_add_mount_pattern(
    protecc_profile_builder_t*   builder,
    const protecc_mount_rule_t*  rule);

/**
 * @brief Compile a profile builder into an opaque compiled profile
 */
protecc_error_t protecc_profile_compile(
    const protecc_profile_builder_t* builder,
    uint32_t                         flags,
    const protecc_compile_config_t*  config,
    protecc_profile_t**             compiled);

/**
 * @brief Create a new compiled pattern set
 * 
 * @param patterns Array of pattern structures (NULL-terminated)
 * @param count Number of patterns in the array
 * @param flags Compilation flags (OR of protecc_flags_t)
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_compile_patterns(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags,
    const protecc_compile_config_t* config,
    protecc_profile_t**            compiled);

/**
 * @brief Match a path against the compiled pattern set
 *
 * If multiple patterns match, the permissions from the most specific
 * (deepest) pattern are returned; ties are combined with bitwise OR.
 * 
 * @param compiled Compiled pattern set
 * @param path Path to match
 * @param requiredPermissions Permissions required for a match to succeed (e.g. PROTECC_PERM_READ)
 * @return true if path matches any pattern, false otherwise
 */
bool protecc_match_path(
    const protecc_profile_t* compiled,
    const char*              path,
    protecc_permission_t     requiredPermissions);

/**
 * @brief Match a runtime network request against compiled network rules
 *
 * Rules are evaluated in insertion order; the first matching rule wins.
 *
 * @param compiled Compiled profile
 * @param request Runtime network request to evaluate
 * @param actionOut Optional output action of the first matching rule
 * @return true if a rule matched, false otherwise
 */
bool protecc_match_net(
    const protecc_profile_t*    compiled,
    const protecc_net_request_t* request,
    protecc_action_t*           actionOut);

/**
 * @brief Match a runtime mount request against compiled mount rules
 *
 * Rules are evaluated in insertion order; the first matching rule wins.
 *
 * @param compiled Compiled profile
 * @param request Runtime mount request to evaluate
 * @param actionOut Optional output action of the first matching rule
 * @return true if a rule matched, false otherwise
 */
bool protecc_match_mount(
    const protecc_profile_t*      compiled,
    const protecc_mount_request_t* request,
    protecc_action_t*             actionOut);

/**
 * @brief Get statistics about the compiled pattern set
 * 
 * @param compiled Compiled pattern set
 * @param stats Output pointer for statistics
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_get_stats(
    const protecc_profile_t* compiled,
    protecc_stats_t*         stats);

/**
 * @brief Export only the path profile from a compiled profile
 * 
 * This function exports the compiled pattern set to a binary format
 * suitable for loading in eBPF programs or other constrained environments.
 * 
 * @param compiled Compiled pattern set
 * @param buffer Output buffer (can be NULL to query size)
 * @param bufferSize Size of output buffer
 * @param bytesWritten Output pointer for actual bytes written
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_profile_export_path(
    const protecc_profile_t* compiled,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten);

/**
 * @brief Export only the network profile from a compiled profile
 */
protecc_error_t protecc_profile_export_net(
    const protecc_profile_t* compiled,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten);

/**
 * @brief Export only the mount profile from a compiled profile
 */
protecc_error_t protecc_profile_export_mounts(
    const protecc_profile_t* compiled,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten);

/**
 * @brief Validate a network profile blob exported by protecc_profile_export_net
 */
protecc_error_t protecc_profile_validate_net_blob(
    const void* buffer,
    size_t      bufferSize);

/**
 * @brief Validate a mount profile blob exported by protecc_profile_export_mounts
 */
protecc_error_t protecc_profile_validate_mount_blob(
    const void* buffer,
    size_t      bufferSize);

/**
 * @brief Import a validated net profile blob into owned typed rules
 */
protecc_error_t protecc_profile_import_net_blob(
    const void*          buffer,
    size_t               bufferSize,
    protecc_net_rule_t** rulesOut,
    size_t*              countOut);

/**
 * @brief Import a validated mount profile blob into owned typed rules
 */
protecc_error_t protecc_profile_import_mount_blob(
    const void*            buffer,
    size_t                 bufferSize,
    protecc_mount_rule_t** rulesOut,
    size_t*                countOut);

/**
 * @brief Free net rules returned by protecc_profile_import_net_blob
 */
void protecc_profile_free_net_rules(
    protecc_net_rule_t* rules,
    size_t              count);

/**
 * @brief Free mount rules returned by protecc_profile_import_mount_blob
 */
void protecc_profile_free_mount_rules(
    protecc_mount_rule_t* rules,
    size_t                count);

/**
 * @brief Initialize a zero-copy net blob view
 */
protecc_error_t protecc_profile_net_view_init(
    const void*               buffer,
    size_t                    bufferSize,
    protecc_net_blob_view_t*  viewOut);

/**
 * @brief Get a decoded net rule by index from zero-copy net blob view
 */
protecc_error_t protecc_profile_net_view_get_rule(
    const protecc_net_blob_view_t* view,
    size_t                         index,
    protecc_net_rule_view_t*       ruleOut);

/**
 * @brief Decode first net rule and initialize iteration index
 */
protecc_error_t protecc_profile_net_view_first(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut);

/**
 * @brief Decode next net rule using iteration index
 */
protecc_error_t protecc_profile_net_view_next(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut);

/**
 * @brief Initialize a zero-copy mount blob view
 */
protecc_error_t protecc_profile_mount_view_init(
    const void*                 buffer,
    size_t                      bufferSize,
    protecc_mount_blob_view_t*  viewOut);

/**
 * @brief Get a decoded mount rule by index from zero-copy mount blob view
 */
protecc_error_t protecc_profile_mount_view_get_rule(
    const protecc_mount_blob_view_t* view,
    size_t                           index,
    protecc_mount_rule_view_t*       ruleOut);

/**
 * @brief Decode first mount rule and initialize iteration index
 */
protecc_error_t protecc_profile_mount_view_first(
    const protecc_mount_blob_view_t* view,
    size_t*                          iterIndexInOut,
    protecc_mount_rule_view_t*       ruleOut);

/**
 * @brief Decode next mount rule using iteration index
 */
protecc_error_t protecc_profile_mount_view_next(
    const protecc_mount_blob_view_t* view,
    size_t*                          iterIndexInOut,
    protecc_mount_rule_view_t*       ruleOut);

/**
 * @brief Import compiled pattern set from binary format
 * 
 * @param buffer Binary data
 * @param bufferSize Size of binary data
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_profile_import_path_blob(
    const void*          buffer,
    size_t               bufferSize,
    protecc_profile_t** compiled);

/**
 * @brief Free a compiled pattern set
 * 
 * @param compiled Compiled pattern set to free
 */
void protecc_free(protecc_profile_t* compiled);

/**
 * @brief Validate a pattern string
 * 
 * @param pattern Pattern string to validate
 * @return PROTECC_OK if pattern is valid, error code otherwise
 */
protecc_error_t protecc_validate_pattern(const char* pattern);

/**
 * @brief Get human-readable error message
 * 
 * @param error Error code
 * @return Error message string
 */
const char* protecc_error_string(protecc_error_t error);

#endif // !__PROTECC_H__
