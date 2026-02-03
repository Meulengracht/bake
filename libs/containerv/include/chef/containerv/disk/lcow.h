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
 * LCOW UVM asset retrieval and caching.
 */
#ifndef __CONTAINERV_DISK_LCOW_H__
#define __CONTAINERV_DISK_LCOW_H__

struct containerv_disk_lcow_uvm_config {
    // URL to a prebuilt LCOW UVM asset archive (typically .zip).
    const char* uvm_url;
};

/**
 * @brief Resolve (download/cache) LCOW UVM assets and return the image path.
 *
 * On success, allocates a string in *image_path_out which must be freed
 * by the caller.
 */
extern int containerv_disk_lcow_resolve_uvm(
    const struct containerv_disk_lcow_uvm_config* config,
    char**                                       image_path_out
);

#endif // !__CONTAINERV_DISK_LCOW_H__
