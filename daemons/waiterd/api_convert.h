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

#ifndef __API_CONVERT_H__
#define __API_CONVERT_H__

#include "chef_waiterd_service.h"
#include "chef_waiterd_cook_service.h"
#include "private.h"

static enum waiterd_architecture waiterd_architecture(enum chef_architecture arch)
{
    switch (arch) {
        case CHEF_ARCHITECTURE_X86: return WAITERD_ARCHITECTURE_X86;
        case CHEF_ARCHITECTURE_X64: return WAITERD_ARCHITECTURE_X64;
        case CHEF_ARCHITECTURE_ARMHF: return WAITERD_ARCHITECTURE_ARMHF;
        case CHEF_ARCHITECTURE_ARM64: return WAITERD_ARCHITECTURE_ARM64;
        case CHEF_ARCHITECTURE_RISCV64: return WAITERD_ARCHITECTURE_RISCV64;
    }
}

static enum waiterd_build_status waiterd_build_status(enum chef_build_status status)
{
    switch (status) {
        case CHEF_BUILD_STATUS_UNKNOWN: return WAITERD_BUILD_STATUS_UNKNOWN;
        case CHEF_BUILD_STATUS_QUEUED: return WAITERD_BUILD_STATUS_QUEUED;
        case CHEF_BUILD_STATUS_SOURCING: return WAITERD_BUILD_STATUS_SOURCING;
        case CHEF_BUILD_STATUS_BUILDING: return WAITERD_BUILD_STATUS_BUILDING;
        case CHEF_BUILD_STATUS_PACKING: return WAITERD_BUILD_STATUS_PACKING;
        case CHEF_BUILD_STATUS_DONE: return WAITERD_BUILD_STATUS_DONE;
        case CHEF_BUILD_STATUS_FAILED: return WAITERD_BUILD_STATUS_FAILED;
    }
}

#endif //!__API_CONVERT_H__
