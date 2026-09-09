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

#ifndef __PLATFORM_ENVIRONMENT_H__
#define __PLATFORM_ENVIRONMENT_H__

#include <chef/list.h>

/**
 * @brief Copy key/value updates into an owned environment list.
 *
 * Keys are matched case-sensitively. Each update replaces the first matching
 * entry (freeing its node, key, and value), then appends a newly allocated copy
 * to the list. Unmatched entries are preserved. Values are copied literally;
 * callers must perform any variable expansion before calling this function.
 *
 * @param environment Initialized, non-NULL list of chef_keypair_item nodes
 *        (defined in chef/platform.h). Nodes, keys, and values must be heap-owned
 *        by this list, with non-NULL keys and values.
 * @param updates Optional list of chef_keypair_item nodes with non-NULL keys and
 *        values. Its contents remain owned by the caller and are not modified.
 *        NULL or the same list as environment is a no-op.
 * @return 0 on success, -1 on allocation failure or invalid input (EINVAL).
 *         Updates applied before a failure remain in environment.
 */
extern int environment_update(struct list* environment, const struct list* updates);

/**
 * @brief Create a process environment by copying a parent and applying overrides.
 *
 * Parent entries whose keys occur in additional are omitted; additional entries
 * are appended as KEY=VALUE strings. Key comparison is case-sensitive.
 *
 * @param parent Non-NULL, NULL-terminated array of KEY=VALUE strings.
 * @param additional Non-NULL, initialized list of chef_keypair_item nodes
 *        (defined in chef/platform.h), with non-NULL keys and values.
 *        An empty list copies the parent without overrides.
 * @return Newly allocated, NULL-terminated array, or NULL on allocation failure.
 *         Release it with environment_destroy(). Neither input is modified.
 */
extern char** environment_create(const char* const* parent, struct list* additional);

/**
 * @brief Replace an existing variable's value with a joined list of values.
 *
 * Despite the name, the current implementation replaces the old value rather
 * than appending to it. The first case-sensitive KEY= match is updated; missing
 * keys are not added. NULL values leaves the existing entry unchanged.
 *
 * @param envp Non-NULL, NULL-terminated array of heap-allocated KEY=VALUE strings.
 *        The replaced string is freed and the array is modified in place.
 * @param key Non-NULL variable name without '='.
 * @param values NULL-terminated array of strings to join, or NULL for no change.
 * @param sep Non-NULL separator inserted between values.
 * @note The key and resulting KEY=VALUE entry must each fit in the implementation's
 *       512-byte formatting buffers, including the terminating NUL.
 * @return 0 when the key is found and processed, -1 if it is missing (ENOENT)
 *         or allocation of the replacement entry fails.
 */
extern int environment_append_keyv(char** envp, char* key, char** values, char* sep);

/**
 * @brief Test whether a process environment contains a key, ignoring case.
 *
 * @param environment NULL-terminated array of KEY=VALUE strings, or NULL.
 * @param key Variable name without '=', or NULL.
 * @return 1 for an exact key match (ignoring case), otherwise 0. NULL inputs
 *         and an empty key return 0. Values do not participate in comparison.
 */
extern int environment_contains_key_insensitive(const char* const* environment, const char* key);

/**
 * @brief Copy a process environment into a contiguous NUL-separated block.
 *
 * Each entry includes its terminating NUL and one extra NUL ends the block.
 * An empty input array produces a single NUL byte.
 *
 * @param environment Non-NULL, NULL-terminated array of KEY=VALUE strings.
 * @param lengthOut Non-NULL output receiving the block size, including all NULs,
 *        on success.
 * @return Heap-allocated block to release with free(), or NULL on allocation
 *         failure. The input array is not modified.
 */
extern char* environment_flatten(const char* const* environment, size_t* lengthOut);

/**
 * @brief Copy a double-NUL-terminated environment block into a string array.
 *
 * @param text NUL-separated entries ending with two consecutive NUL bytes, or
 *        NULL. The block must be readable through its double-NUL terminator;
 *        no length validation is performed. A two-NUL empty block currently
 *        produces one empty string entry. A single-NUL block is not valid input.
 * @return Newly allocated, NULL-terminated array to release with
 *         environment_destroy(), or NULL for NULL input or allocation failure.
 *         The input block is not modified.
 */
extern char** environment_unflatten(const char* text);

/**
 * @brief Free every string in a process environment and then the array itself.
 *
 * @param environment Heap-allocated, NULL-terminated array, typically returned
 *        by environment_create() or environment_unflatten(). NULL is accepted.
 *        This does not free a list of chef_keypair_item nodes.
 */
extern void environment_destroy(char** environment);

#endif //!__PLATFORM_ENVIRONMENT_H__
