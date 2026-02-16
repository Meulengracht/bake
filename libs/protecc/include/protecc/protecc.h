/**
 * @file protecc.h
 * @brief Path pattern matching library optimized for eBPF evaluation
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

#ifndef PROTECC_H
#define PROTECC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Statistics about compiled patterns
 */
typedef struct {
    size_t num_patterns;      /**< Number of patterns compiled */
    size_t binary_size;       /**< Size of compiled binary in bytes */
    size_t max_depth;         /**< Maximum trie depth */
    size_t num_nodes;         /**< Number of trie nodes */
} protecc_stats_t;

/**
 * @brief Pattern entry with explicit permissions
 */
typedef struct {
    const char* pattern;      /**< Pattern string */
    uint32_t permissions;     /**< OR of protecc_permission_t */
} protecc_pattern_t;

/**
 * @brief Create a new compiled pattern set
 * 
 * @param patterns Array of pattern strings (NULL-terminated)
 * @param count Number of patterns in the array
 * @param flags Compilation flags (OR of protecc_flags_t)
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_compile(
    const char** patterns,
    size_t count,
    uint32_t flags,
    protecc_compiled_t** compiled
);

/**
 * @brief Create a new compiled pattern set with explicit permissions
 *
 * @param patterns Array of pattern entries
 * @param count Number of patterns in the array
 * @param flags Compilation flags (OR of protecc_flags_t)
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_compile_with_permissions(
    const protecc_pattern_t* patterns,
    size_t count,
    uint32_t flags,
    protecc_compiled_t** compiled
);

/**
 * @brief Match a path against the compiled pattern set
 * 
 * @param compiled Compiled pattern set
 * @param path Path to match
 * @param path_len Length of path (or 0 to use strlen)
 * @return true if path matches any pattern, false otherwise
 */
bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len
);

/**
 * @brief Match a path against compiled patterns requiring specific permissions
 *
 * @param compiled Compiled pattern set
 * @param path Path to match
 * @param path_len Length of path (or 0 to use strlen)
 * @param required_permissions OR of protecc_permission_t required by caller
 * @return true if path matches a pattern that grants all required permissions
 */
bool protecc_match_with_permissions(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    uint32_t required_permissions
);

/**
 * @brief Get statistics about the compiled pattern set
 * 
 * @param compiled Compiled pattern set
 * @param stats Output pointer for statistics
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t* stats
);

/**
 * @brief Export compiled pattern set to binary format
 * 
 * This function exports the compiled pattern set to a binary format
 * suitable for loading in eBPF programs or other constrained environments.
 * 
 * @param compiled Compiled pattern set
 * @param buffer Output buffer (can be NULL to query size)
 * @param buffer_size Size of output buffer
 * @param bytes_written Output pointer for actual bytes written
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_written
);

/**
 * @brief Import compiled pattern set from binary format
 * 
 * @param buffer Binary data
 * @param buffer_size Size of binary data
 * @param compiled Output pointer for the compiled pattern set
 * @return PROTECC_OK on success, error code otherwise
 */
protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
);

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

#ifdef __cplusplus
}
#endif

#endif /* PROTECC_H */
