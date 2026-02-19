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

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "../private.h"

static bool __valid_dfa(const protecc_profile_t* compiled) {
    if (!compiled || !compiled->has_dfa) {
        return false;
    }
    if (!compiled->dfa_transitions || !compiled->dfa_accept || !compiled->dfa_perms) {
        return false;
    }
    if (compiled->dfa_num_states == 0 || compiled->dfa_num_classes == 0) {
        return false;
    }
    return true;
}

bool __matcher_dfa(
    const protecc_profile_t* compiled,
    const char*              path,
    protecc_permission_t     requiredPermissions)
{
    uint32_t state;

    if (!__valid_dfa(compiled)) {
        return false;
    }

    state = compiled->dfa_start_state;
    for (size_t i = 0; path[i]; i++) {
        uint8_t  c   = (uint8_t)path[i];
        uint32_t cls = compiled->dfa_classmap[c];
        uint64_t index;

        if (cls >= compiled->dfa_num_classes) {
            return false;
        }

        index = ((uint64_t)state * (uint64_t)compiled->dfa_num_classes) + (uint64_t)cls;
        state = compiled->dfa_transitions[index];
        if (state >= compiled->dfa_num_states) {
            return false;
        }
    }

    if ((compiled->dfa_accept[state >> 5] & (1u << (state & 31u))) == 0u) {
        return false;
    }

    if ((compiled->dfa_perms[state] & requiredPermissions) != requiredPermissions) {
        return false;
    }
    return true;
}
