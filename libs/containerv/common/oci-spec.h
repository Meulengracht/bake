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

#ifndef __CONTAINERV_OCI_SPEC_H__
#define __CONTAINERV_OCI_SPEC_H__

// Internal helper for generating OCI runtime-spec JSON used by LCOW (OCI-in-UVM).
// Not part of the public API.

struct containerv_oci_linux_spec_params {
    // JSON array string of args, e.g. ["/bin/sh","-lc","echo hi"].
    const char* args_json;

    // Array of KEY=VALUE strings (NULL-terminated). Optional.
    const char* const* envv;

    // OCI root.path (e.g. "/chef/rootfs"). Required.
    const char* root_path;

    // process.cwd (e.g. "/"). Optional; defaults to "/".
    const char* cwd;

    // hostname (optional).
    const char* hostname;
};

int containerv_oci_build_linux_spec_json(
    const struct containerv_oci_linux_spec_params* params,
    char**                                         jsonOut
);

#endif // !__CONTAINERV_OCI_SPEC_H__
