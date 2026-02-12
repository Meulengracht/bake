/**
 * @file matcher.c
 * @brief Pattern matching implementation for protecc library
 */

#include "protecc_internal.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/**
 * @brief Check if a character matches a node
 */
static bool char_matches_node(
    const protecc_node_t* node,
    char c,
    bool case_insensitive
) {
    if (case_insensitive) {
        c = tolower(c);
    }
    
    switch (node->type) {
        case NODE_LITERAL:
            return c == node->data.literal;
        
        case NODE_WILDCARD_SINGLE:
            return c != '\0';
        
        case NODE_CHARSET:
            return protecc_charset_contains(&node->data.charset, (unsigned char)c);
        
        case NODE_RANGE:
            if (case_insensitive) {
                return (c >= tolower(node->data.range.start) && 
                        c <= tolower(node->data.range.end));
            }
            return (c >= node->data.range.start && c <= node->data.range.end);
        
        default:
            return false;
    }
}

/**
 * @brief Match with modifier support
 */
static bool match_with_modifier(
    const protecc_node_t* node,
    const char* path,
    size_t path_len,
    size_t pos,
    const protecc_node_t* next_node,
    bool case_insensitive
) {
    switch (node->modifier) {
        case MODIFIER_OPTIONAL: // ? - 0 or 1
            // Try matching with the character
            if (pos < path_len && char_matches_node(node, path[pos], case_insensitive)) {
                if (next_node) {
                    if (protecc_match_internal(next_node, path, path_len, pos + 1)) {
                        return true;
                    }
                } else if (pos + 1 == path_len) {
                    return node->is_terminal;
                }
            }
            // Try matching without the character
            if (next_node) {
                return protecc_match_internal(next_node, path, path_len, pos);
            }
            return node->is_terminal && pos == path_len;
        
        case MODIFIER_ONE_OR_MORE: // + - 1 or more
            // Must match at least once
            if (pos >= path_len || !char_matches_node(node, path[pos], case_insensitive)) {
                return false;
            }
            pos++;
            // Fall through to zero-or-more logic
        
        case MODIFIER_ZERO_OR_MORE: // * - 0 or more
            // Try matching as many as possible
            while (pos < path_len && char_matches_node(node, path[pos], case_insensitive)) {
                // Try continuing from here
                if (next_node) {
                    if (protecc_match_internal(next_node, path, path_len, pos)) {
                        return true;
                    }
                } else if (node->is_terminal && pos == path_len) {
                    return true;
                }
                pos++;
            }
            // Try without consuming any more
            if (next_node) {
                return protecc_match_internal(next_node, path, path_len, pos);
            }
            return node->is_terminal && pos == path_len;
        
        case MODIFIER_NONE:
        default:
            return false; // Should not reach here
    }
}

bool protecc_match_internal(
    const protecc_node_t* node,
    const char* path,
    size_t path_len,
    size_t pos
) {
    if (!node) {
        return false;
    }
    
    // If we're at a terminal and consumed all input, match!
    if (pos == path_len && node->is_terminal) {
        return true;
    }
    
    // If we have no more children and no more input, check if terminal
    if (node->num_children == 0) {
        return pos == path_len && node->is_terminal;
    }
    
    // Try matching each child
    for (size_t i = 0; i < node->num_children; i++) {
        protecc_node_t* child = node->children[i];
        
        // Handle modifiers
        if (child->modifier != MODIFIER_NONE) {
            protecc_node_t* next = (i + 1 < node->num_children) ? node->children[i + 1] : NULL;
            if (match_with_modifier(child, path, path_len, pos, next, false)) {
                return true;
            }
            continue;
        }
        
        switch (child->type) {
            case NODE_WILDCARD_RECURSIVE: {
                // ** matches anything including /
                // Try matching from every position
                for (size_t try_pos = pos; try_pos <= path_len; try_pos++) {
                    if (protecc_match_internal(child, path, path_len, try_pos)) {
                        return true;
                    }
                }
                break;
            }
            
            case NODE_WILDCARD_MULTI: {
                // * matches anything except /
                size_t try_pos = pos;
                while (try_pos <= path_len) {
                    if (protecc_match_internal(child, path, path_len, try_pos)) {
                        return true;
                    }
                    if (try_pos < path_len && path[try_pos] == '/') {
                        break; // Stop at directory separator
                    }
                    try_pos++;
                }
                break;
            }
            
            case NODE_WILDCARD_SINGLE:
            case NODE_LITERAL:
            case NODE_CHARSET:
            case NODE_RANGE: {
                // Match single character
                if (pos < path_len && char_matches_node(child, path[pos], false)) {
                    if (protecc_match_internal(child, path, path_len, pos + 1)) {
                        return true;
                    }
                }
                break;
            }
            
            default:
                break;
        }
    }
    
    return false;
}
