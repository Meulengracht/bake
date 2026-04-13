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

#ifndef __CHEF_PACKAGE_IMAGE_H__
#define __CHEF_PACKAGE_IMAGE_H__

#include <chef/list.h>
#include <chef/package_manifest.h>

/**
 * @brief Package image construction API.
 *
 * This layer is responsible for creating a `.pack` image from a staged file
 * tree and a canonical package manifest. It does not define or own the
 * metadata model itself; see `chef/package_manifest.h` for that API.
 */

/**
 * @brief Options for constructing a `.pack` image from an input directory.
 */
struct chef_package_image_options {
    const char*                         input_dir;
    const char*                         output_path;
    const struct list*                  filters; // list<list_item_string>
    const struct chef_package_manifest* manifest;
};

/**
 * @brief Creates a package image from a staged filesystem tree and manifest.
 *
 * The input directory is copied into a new VaFs image and the supplied Chef
 * manifest is written into the image metadata.
 *
 * @return 0 on success, -1 on failure with errno set.
 */
extern int chef_package_image_create(const struct chef_package_image_options* options);

#endif //!__CHEF_PACKAGE_IMAGE_H__