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

#ifndef __LIBCHEF_API_PACKAGE_H__
#define __LIBCHEF_API_PACKAGE_H__

#include <chef/package.h>
#include <stdio.h>

/**
 * @brief Parameters for retrieving package information.
 */
struct chef_info_params {
    const char* publisher; /**< The publisher/owner of the package */
    const char* package;   /**< The package name */
};

/**
 * @brief Parameters for searching/finding packages.
 */
struct chef_find_params {
    const char* query;      /**< The search query string */
    int         privileged; /**< Whether to include privileged/private packages */
};

/**
 * @brief Parameters for publishing a package.
 */
struct chef_publish_params {
    const char*          publisher;    /**< The publisher/owner of the package */
    const char*          package;      /**< The package name */
    const char*          platform;     /**< The target platform (e.g., "linux", "windows") */
    const char*          architecture; /**< The target architecture (e.g., "amd64", "arm64") */
    const char*          channel;      /**< The release channel (e.g., "stable", "dev") */
    struct chef_version* version;      /**< The version information for this package */
};

/**
 * @brief Parameters for downloading a package.
 */
struct chef_download_params {
    const char*          publisher;    /**< The publisher/owner of the package */
    const char*          package;      /**< The package name */
    const char*          platform;     /**< The target platform (e.g., "linux", "windows") */
    const char*          arch;         /**< The target architecture (e.g., "amd64", "arm64") */
    const char*          channel;      /**< The release channel (e.g., "stable", "dev") */

    // this will be updated to the revision downloaded if 0
    int                  revision;     /**< The specific revision to download. If 0, downloads the latest and updates this field */
};

/**
 * @brief Parameters for retrieving package proof/verification.
 */
struct chef_proof_params {
    const char* publisher; /**< The publisher/owner of the package */
    const char* package;   /**< The package name */
    int         revision;  /**< The specific revision to get proof for */
};

/**
 * @brief Result structure returned from package search operations.
 */
struct chef_find_result {
    const char*            publisher;        /**< The publisher/owner of the package */
    const char*            package;          /**< The package name */
    const char*            summary;          /**< Brief description of the package */
    enum chef_package_type type;             /**< The type of package (application, library, etc.) */
    const char*            maintainer;       /**< The maintainer's name */
    const char*            maintainer_email; /**< The maintainer's email address */
};

/**
 * @brief Downloads a package from the Chef package repository.
 * 
 * This function downloads a specific package revision based on the provided parameters.
 * If the revision is set to 0, it will download the latest available revision and update
 * the revision field in the params structure.
 * 
 * @param[In]  params A pointer to the download parameters specifying which package to download
 * @param[In]  path   The local file path where the downloaded package should be saved
 * @return int        Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chefclient_pack_download(struct chef_download_params* params, const char* path);

/**
 * @brief Retrieves cryptographic proof/verification data for a package revision.
 * 
 * This function fetches the cryptographic proof data for a specific package revision,
 * which can be used to verify the integrity and authenticity of the package.
 * 
 * @param[In]  params A pointer to the proof parameters specifying which package revision to verify
 * @param[In]  stream The file stream where the proof data will be written
 * @return int        Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chefclient_pack_proof(struct chef_proof_params* params, FILE* stream);

/**
 * @brief Retrieves detailed information about a specific package.
 * 
 * This function fetches comprehensive metadata about a package, including all available
 * revisions, versions, platforms, and channels. The returned structure must be freed
 * using the appropriate chef_package_free function.
 * 
 * @param[In]  params     A pointer to the info parameters specifying which package to query
 * @param[Out] packageOut A pointer where the allocated package structure will be stored
 * @return int            Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chefclient_pack_info(struct chef_info_params* params, struct chef_package** packageOut);

/**
 * @brief Searches for packages matching the specified query.
 * 
 * This function performs a search across the Chef package repository and returns
 * an array of matching packages. The results array and its contents must be freed
 * using chefclient_pack_find_free when no longer needed.
 * 
 * @param[In]  params  A pointer to the find parameters containing the search query
 * @param[Out] results A pointer where the array of search results will be stored
 * @param[Out] count   A pointer where the number of results will be stored
 * @return int         Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int  chefclient_pack_find(struct chef_find_params* params, struct chef_find_result*** results, int* count);

/**
 * @brief Frees the memory allocated by chefclient_pack_find.
 * 
 * This function properly deallocates all memory associated with the search results,
 * including the result structures themselves and any strings they contain.
 * 
 * @param[In] results The array of search results to free
 * @param[In] count   The number of results in the array
 */
extern void chefclient_pack_find_free(struct chef_find_result** results, int count);

/**
 * @brief Publishes a package to the Chef package repository.
 * 
 * This function uploads and publishes a package to the repository. Authentication
 * is required - chefclient_login must be called before using this function.
 * The package file at the specified path will be uploaded along with its metadata.
 * 
 * @param[In] params A pointer to the publish parameters containing package metadata
 * @param[In] path   The local file path to the package file to be published
 * @return int       Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chefclient_pack_publish(struct chef_publish_params* params, const char* path);

#endif //!__LIBCHEF_API_PACKAGE_H__
