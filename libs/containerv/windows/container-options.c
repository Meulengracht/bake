/**
 * Copyright 2024, Philip Meulengracht
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

#include <chef/containerv.h>
#include <stdlib.h>
#include "private.h"

struct containerv_options* containerv_options_new(void)
{
    struct containerv_options* options = calloc(1, sizeof(struct containerv_options));
    if (options == NULL) {
        return NULL;
    }
    return options;
}

void containerv_options_delete(struct containerv_options* options)
{
    if (options) {
        if (options->mounts) {
            free(options->mounts);
        }
        free(options);
    }
}

void containerv_options_set_caps(struct containerv_options* options, enum containerv_capabilities caps)
{
    if (options) {
        options->capabilities = caps;
    }
}

void containerv_options_set_mounts(struct containerv_options* options, struct containerv_mount* mounts, int count)
{
    if (options) {
        options->mounts = mounts;
        options->mounts_count = count;
    }
}
