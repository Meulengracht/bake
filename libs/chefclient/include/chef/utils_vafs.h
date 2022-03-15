/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __LIBCHEF_UTILS_VAFS_H__
#define __LIBCHEF_UTILS_VAFS_H__

#include <stddef.h>
#include <chef/package.h>
#include <vafs/vafs.h>

#define CHEF_PACKAGE_HEADER_GUID  { 0x91C48A1D, 0xC445, 0x4607, { 0x95, 0x98, 0xFE, 0x73, 0x49, 0x1F, 0xD3, 0x7E } }
#define CHEF_PACKAGE_VERSION_GUID { 0x478ED773, 0xAA23, 0x45DA, { 0x89, 0x23, 0x9F, 0xCE, 0x5F, 0x2E, 0xCB, 0xED } }

struct chef_vafs_feature_package_header {
    struct VaFsFeatureHeader header;

    enum chef_package_type   type;

    // lengths of the data for each string, none of the strings
    // are zero terminated, which must be added at load
    size_t                   package_length;
    size_t                   description_length;
    size_t                   homepage_length;
    size_t                   license_length;
    size_t                   maintainer_length;
    size_t                   maintainer_email_length;
};

struct chef_vafs_feature_package_version {
    struct VaFsFeatureHeader header;
    int                      major;
    int                      minor;
    int                      revision;

    // the data is not zero terminated.
    size_t                   tag_length;
};

#endif //!__LIBCHEF_UTILS_VAFS_H__
