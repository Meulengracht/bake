/**
 * @file parser.c
 * @brief Pattern parsing implementation for protecc library
 */

#include "protecc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Parse a character set like [abc] or [a-z]
 */
static protecc_error_t parse_charset(
    const char** pattern,
    protecc_node_t* node,
    uint32_t flags
) {
    const char* p = *pattern;
    
    if (*p != '[') {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    p++; // skip '['
    
    memset(&node->data.charset, 0, sizeof(protecc_charset_t));
    
    while (*p && *p != ']') {
        if (*(p + 1) == '-' && *(p + 2) != ']' && *(p + 2) != '\0') {
            // Range like a-z or 0-9
            char start = *p;
            char end = *(p + 2);
            
            if (flags & PROTECC_FLAG_CASE_INSENSITIVE) {
                start = tolower(start);
                end = tolower(end);
            }
            
            if (start > end) {
                return PROTECC_ERROR_INVALID_PATTERN;
            }
            
            protecc_charset_set_range(&node->data.charset, start, end);
            
            // Also add uppercase range if case insensitive
            if ((flags & PROTECC_FLAG_CASE_INSENSITIVE) && isalpha(start)) {
                protecc_charset_set_range(
                    &node->data.charset,
                    toupper(start),
                    toupper(end)
                );
            }
            
            p += 3; // skip 'a-z'
        } else {
            // Single character
            char c = *p;
            if (flags & PROTECC_FLAG_CASE_INSENSITIVE) {
                protecc_charset_set(&node->data.charset, tolower(c));
                if (isalpha(c)) {
                    protecc_charset_set(&node->data.charset, toupper(c));
                }
            } else {
                protecc_charset_set(&node->data.charset, c);
            }
            p++;
        }
    }
    
    if (*p != ']') {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    p++; // skip ']'
    
    *pattern = p;
    return PROTECC_OK;
}

/**
 * @brief Check if character is a modifier
 */
static bool is_modifier(char c) {
    return c == '?' || c == '+' || c == '*';
}

/**
 * @brief Parse a modifier
 */
static protecc_modifier_t parse_modifier(const char** pattern) {
    char c = **pattern;
    if (c == '?') {
        (*pattern)++;
        return MODIFIER_OPTIONAL;
    } else if (c == '+') {
        (*pattern)++;
        return MODIFIER_ONE_OR_MORE;
    } else if (c == '*') {
        (*pattern)++;
        return MODIFIER_ZERO_OR_MORE;
    }
    return MODIFIER_NONE;
}

protecc_error_t protecc_parse_pattern(
    const char* pattern,
    protecc_node_t* root,
    uint32_t flags
) {
    if (!pattern || !root) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    const char* p = pattern;
    protecc_node_t* current = root;
    
    while (*p) {
        protecc_node_t* node = NULL;
        
        if (*p == '*' && *(p + 1) == '*') {
            // ** - recursive wildcard
            node = protecc_node_new(NODE_WILDCARD_RECURSIVE);
            if (!node) {
                return PROTECC_ERROR_OUT_OF_MEMORY;
            }
            p += 2;
            
            // Skip optional '/' after **
            if (*p == '/') {
                p++;
            }
        } else if (*p == '*') {
            // * - wildcard (no /)
            node = protecc_node_new(NODE_WILDCARD_MULTI);
            if (!node) {
                return PROTECC_ERROR_OUT_OF_MEMORY;
            }
            p++;
        } else if (*p == '?') {
            // ? - single character wildcard
            node = protecc_node_new(NODE_WILDCARD_SINGLE);
            if (!node) {
                return PROTECC_ERROR_OUT_OF_MEMORY;
            }
            p++;
        } else if (*p == '[') {
            // Character set or range
            node = protecc_node_new(NODE_CHARSET);
            if (!node) {
                return PROTECC_ERROR_OUT_OF_MEMORY;
            }
            
            protecc_error_t err = parse_charset(&p, node, flags);
            if (err != PROTECC_OK) {
                protecc_node_free(node);
                return err;
            }
            
            // Check for modifier after charset
            if (*p && is_modifier(*p)) {
                // Treat *, + and ? uniformly as quantifiers for charset tokens.
                node->modifier = parse_modifier(&p);
            }
        } else {
            // Literal character
            node = protecc_node_new(NODE_LITERAL);
            if (!node) {
                return PROTECC_ERROR_OUT_OF_MEMORY;
            }
            
            char c = *p;
            if (flags & PROTECC_FLAG_CASE_INSENSITIVE) {
                c = tolower(c);
            }
            node->data.literal = c;
            p++;
        }
        
        if (!node) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        
        // Add node to current
        protecc_error_t err = protecc_node_add_child(current, node);
        if (err != PROTECC_OK) {
            protecc_node_free(node);
            return err;
        }
        
        current = node;
    }
    
    // Mark the last node as terminal
    if (current != root) {
        current->is_terminal = true;
    }
    
    return PROTECC_OK;
}
