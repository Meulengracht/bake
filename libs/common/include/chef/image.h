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

#ifndef __CHEF_IMAGE_H__
#define __CHEF_IMAGE_H__

#include <chef/list.h>
#include <stddef.h>

enum chef_image_source_type {
    CHEF_IMAGE_SOURCE_INVALID,
    CHEF_IMAGE_SOURCE_FILE,
    CHEF_IMAGE_SOURCE_DIRECTORY,
    CHEF_IMAGE_SOURCE_PACKAGE
};

struct chef_image_parition_source {
    struct list_item            list_header;
    enum chef_image_source_type type;
    const char*                 source;
    const char*                 target;
};

struct chef_image_partition {
    struct list_item list_header;

    const char*  label;
    const char*  fstype;
    const char*  guid;
    long long    size;
    struct list  attributes; //list<list_item_string> 

    // A partition either has a chef package as content
    // or a list of sources. Content is unpacked based on it's
    // type (i.e. BOOTLOADER) or installed raw if under sources.
    const char* content;
    struct list sources; // list<chef_image_parition_source>
};

enum chef_image_schema {
    CHEF_IMAGE_SCHEMA_INVALID,
    CHEF_IMAGE_SCHEMA_MBR,
    CHEF_IMAGE_SCHEMA_GPT
};

struct chef_image {
    enum chef_image_schema schema;
    struct list            partitions; // list<chef_image_partition>
};

/**
 * @brief Parses a recipe from a yaml file buffer.
 * 
 * @param[In]  buffer 
 * @param[In]  length 
 * @param[Out] recipeOut
 * @return int 
 */
extern int chef_image_parse(void* buffer, size_t length, struct chef_image** imageOut);

/**
 * @brief Cleans up any resources allocated during recipe_parse, and frees the recipe.
 * 
 * @param[In] recipe 
 */
extern void chef_image_destroy(struct chef_image* image);

#endif //!__CHEF_IMAGE_H__
