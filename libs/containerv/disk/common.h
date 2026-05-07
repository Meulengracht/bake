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
 */

#ifndef __CONTAINERV_DISK_COMMON_H__
#define __CONTAINERV_DISK_COMMON_H__

#include <stdint.h>

/**
 * @brief Check whether a path currently exists.
 * @param path Path to probe.
 * @return 1 when the path exists, otherwise 0.
 */
extern int containerv_disk_path_exists(const char* path);

/**
 * @brief Check whether a path exists and is a directory.
 * @param path Path to probe.
 * @return 1 when the path exists and is a directory, otherwise 0.
 */
extern int containerv_disk_path_is_directory(const char* path);

/**
 * @brief Compute a stable FNV-1a 64-bit hash for cache-key generation.
 * @param text Input string. NULL hashes as the FNV offset basis.
 * @return The computed 64-bit hash.
 */
extern uint64_t containerv_disk_fnv1a64(const char* text);

/**
 * @brief Create or overwrite a small readiness marker file.
 * @param marker_path Path to the marker file to write.
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_write_marker(const char* marker_path);

/**
 * @brief Download an archive into a cache directory unless it already exists.
 * @param cache_dir Cache directory that holds the archive.
 * @param archive_name File name to use inside the cache directory.
 * @param url Source URL to download from when the archive is missing.
 * @param archive_path_out Receives the full cached archive path on success.
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_cache_archive(
    const char* cache_dir,
    const char* archive_name,
    const char* url,
    char**      archive_path_out);

/**
 * @brief Extract a cached archive into a destination directory.
 * @param archive_path Archive to unpack.
 * @param destination Extraction destination.
 * @param recreate_destination When non-zero, remove and recreate destination first.
 * @param include_xattrs When non-zero, preserve xattrs during extraction.
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_extract_archive(
    const char* archive_path,
    const char* destination,
    int         recreate_destination,
    int         include_xattrs);

#endif