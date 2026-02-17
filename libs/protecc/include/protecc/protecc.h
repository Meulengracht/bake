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
 * @brief Opaque handle to a compiled pattern set
 */
typedef struct protecc_compiled protecc_compiled_t;

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
} protecc_error_t;

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
 * @brief Create a new compiled pattern set
 * 
 * @param patterns Array of pattern structures (NULL-terminated) ninja -C build protecc_test && ./build/libs/protecc/protecc_test
 * @param count Number of patterns in the array
 * @param flags Compilation flags (OR of protecc_flags_t)
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_compile(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags,
    const protecc_compile_config_t* config,
    protecc_compiled_t**            compiled);

/**
 * @brief Match a path against the compiled pattern set
 *
 * If multiple patterns match, the permissions from the most specific
 * (deepest) pattern are returned; ties are combined with bitwise OR.
 * 
 * @param compiled Compiled pattern set
 * @param path Path to match
 * @param pathLength Length of path (or 0 to use strlen)
 * @param perms_out Output pointer for matched permissions
 * @return true if path matches any pattern, false otherwise
 */
bool protecc_match(
    const protecc_compiled_t* compiled,
    const char*               path,
     size_t                    pathLength,
     protecc_permission_t*     perms_out);

/**
 * @brief Get statistics about the compiled pattern set
 * 
 * @param compiled Compiled pattern set
 * @param stats Output pointer for statistics
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t*          stats);

/**
 * @brief Export compiled pattern set to binary format
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
protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten);

/**
 * @brief Import compiled pattern set from binary format
 * 
 * @param buffer Binary data
 * @param bufferSize Size of binary data
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_import(
    const void*          buffer,
    size_t               bufferSize,
    protecc_compiled_t** compiled);

/**
 * @brief Free a compiled pattern set
 * 
 * @param compiled Compiled pattern set to free
 */
void protecc_free(protecc_compiled_t* compiled);

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
