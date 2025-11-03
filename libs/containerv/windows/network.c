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

#include <windows.h>
#include <vlog.h>

#include "private.h"

/**
 * Windows container networking using HNS (Host Network Service)
 * This provides NAT networking for containers
 */

int containerv_network_initialize(struct containerv_container* container)
{
    // TODO: Implement Windows HNS networking
    // - Create HNS network
    // - Attach container to network
    // - Configure NAT
    VLOG_WARNING("containerv", "Windows container networking not yet implemented\n");
    return 0;
}

int containerv_network_cleanup(struct containerv_container* container)
{
    // TODO: Cleanup HNS network resources
    VLOG_WARNING("containerv", "Windows container networking cleanup not yet implemented\n");
    return 0;
}
