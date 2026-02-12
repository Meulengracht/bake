/**
 * @file protecc_internal.h
 * @brief Internal structures and definitions for protecc library
 */

#ifndef PROTECC_INTERNAL_H
#define PROTECC_INTERNAL_H

#include <protecc/protecc.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Node type in the pattern trie
 */
typedef enum {
    NODE_LITERAL = 0,      /**< Exact character match */
    NODE_WILDCARD_SINGLE,  /**< ? - matches any single char */
    NODE_WILDCARD_MULTI,   /**< * - matches any chars (no /) */
    NODE_WILDCARD_RECURSIVE, /**< ** - matches any chars (with /) */
    NODE_CHARSET,          /**< [abc] - character set */
    NODE_RANGE,            /**< [a-z] or [0-9] - character range */
    NODE_GROUP,            /**< Group with modifiers (?, +, *) */
} protecc_node_type_t;

/**
 * @brief Modifier for pattern nodes
 */
typedef enum {
    MODIFIER_NONE = 0,     /**< No modifier */
    MODIFIER_OPTIONAL,     /**< ? - 0 or 1 */
    MODIFIER_ONE_OR_MORE,  /**< + - 1 or more */
    MODIFIER_ZERO_OR_MORE, /**< * - 0 or more */
} protecc_modifier_t;

/**
 * @brief Character range for NODE_RANGE
 */
typedef struct {
    char start;
    char end;
} protecc_range_t;

/**
 * @brief Character set for NODE_CHARSET
 */
#define MAX_CHARSET_SIZE 256
typedef struct {
    uint8_t chars[MAX_CHARSET_SIZE / 8];  /**< Bitmap of characters */
} protecc_charset_t;

/**
 * @brief Forward declaration
 */
typedef struct protecc_node protecc_node_t;

/**
 * @brief Trie node representing part of a pattern
 */
struct protecc_node {
    protecc_node_type_t type;
    protecc_modifier_t modifier;
    
    union {
        char literal;                /**< For NODE_LITERAL */
        protecc_range_t range;       /**< For NODE_RANGE */
        protecc_charset_t charset;   /**< For NODE_CHARSET */
    } data;
    
    protecc_node_t** children;       /**< Array of child nodes */
    size_t num_children;
    size_t capacity_children;
    
    bool is_terminal;                /**< True if this ends a pattern */
};

/**
 * @brief Compiled pattern set structure
 */
struct protecc_compiled {
    protecc_node_t* root;            /**< Root of the trie */
    uint32_t flags;                  /**< Compilation flags */
    protecc_stats_t stats;           /**< Statistics */
};

/**
 * @brief Create a new trie node
 */
protecc_node_t* protecc_node_new(protecc_node_type_t type);

/**
 * @brief Free a trie node and its children
 */
void protecc_node_free(protecc_node_t* node);

/**
 * @brief Add a child node to a parent node
 */
protecc_error_t protecc_node_add_child(protecc_node_t* parent, protecc_node_t* child);

/**
 * @brief Parse a pattern string into a trie
 */
protecc_error_t protecc_parse_pattern(
    const char* pattern,
    protecc_node_t* root,
    uint32_t flags
);

/**
 * @brief Set a character in a charset
 */
void protecc_charset_set(protecc_charset_t* charset, unsigned char c);

/**
 * @brief Check if a character is in a charset
 */
bool protecc_charset_contains(const protecc_charset_t* charset, unsigned char c);

/**
 * @brief Set a range of characters in a charset
 */
void protecc_charset_set_range(protecc_charset_t* charset, char start, char end);

/**
 * @brief Match path against a trie starting from a specific node
 */
bool protecc_match_internal(
    const protecc_node_t* node,
    const char* path,
    size_t path_len,
    size_t pos
);

#endif /* PROTECC_INTERNAL_H */
