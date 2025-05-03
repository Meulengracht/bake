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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

struct __container {
    char* id;
};

static struct {
    struct list containers;
} g_server = { 0 };

static int __generate_container_path(char** path)
{
    char   tmp_path[PATH_MAX];
    char   random_id[17]; // 16 chars + null terminator
    time_t t;
    int    i;
    
    // Initialize random seed
    srand((unsigned int)time(&t));
    
    // Generate a random 16 character hex string
    for (i = 0; i < 16; i++) {
        int r = rand() % 16;
        random_id[i] = r < 10 ? '0' + r : 'a' + (r - 10);
    }
    random_id[16] = '\0';
    
    // Create the path
    if (snprintf(tmp_path, PATH_MAX, "/tmp/chef/cvd/%s", random_id) >= PATH_MAX) {
        return -1; // Path too long
    }
    
    // Create the base directory if it doesn't exist
    if (platform_mkdir("/tmp/chef", 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    if (platform_mkdir("/tmp/chef/cvd", 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    // Allocate and copy the path
    *path = strdup(tmp_path);
    if (*path == NULL) {
        return -1; // Out of memory
    }
    return 0;
}

static int __create_container_directory(char* path)
{
    return platform_mkdir(path, 0755);
}

static int __run_debootstrap(const char* where)
{
    char command[PATH_MAX * 2];
    int  status;
    
    // Construct the debootstrap command with --variant=minbase
    // This creates a minimal Debian-based system
    if (snprintf(command, sizeof(command), 
                "debootstrap --variant=minbase stable %s", where) >= sizeof(command)) {
        return -1; // Command too long
    }
    
    // Execute the debootstrap command
    status = system(command);
    if (status != 0) {
        VLOG_ERROR("cvd", "Failed to run debootstrap command: %s (status: %d)\n", 
                  command, status);
        return -1;
    }
    
    VLOG_INFO("cvd", "Successfully created minimal rootfs at %s\n", where);
    return 0;
}

static int __resolve_rootfs(const struct chef_create_parameters* params, char** path)
{
    switch (params->type) {
        case CHEF_ROOTFS_TYPE_DEBOOTSTRAP:
        // Create a new rootfs using debootstrap from the host
        {
            char* container_path = NULL;
            int   status;
            
            // Generate a random container path
            status = __generate_container_path(&container_path);
            if (status != 0) {
                return status;
            }
            
            // Create the directory
            status = __create_container_directory(container_path);
            if (status != 0) {
                free(container_path);
                return status;
            }
            
            // Execute debootstrap to create the rootfs
            status = __run_debootstrap(container_path, params->rootfs);
            if (status != 0) {
                platform_rmdir(container_path);
                free(container_path);
                return status;
            }
            
            // Set the rootfs path
            *path = container_path;
            return 0;
        }
        case CHEF_ROOTFS_TYPE_OSBASE:
        case CHEF_ROOTFS_TYPE_IMAGE:
    }
}

static int __resolve_mounts()
{

}

enum chef_status cvd_create(const struct chef_create_parameters* params, char* const* id)
{
    char* rootfs;
    int   status;

    // resolve the type of roots
    status = __resolve_rootfs(params, &rootfs);
    if (status) {
        return ;
    }

    // setup mounts
    status = __resolve_mounts();
    if (status) {
        return ;
    }

    // setup other config

    // create the container

    return CHEF_STATUS_SUCCESS;
}
