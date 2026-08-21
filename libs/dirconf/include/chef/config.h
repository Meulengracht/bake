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

#ifndef __CHEF_CONFIG_H__
#define __CHEF_CONFIG_H__

struct chef_config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

// keep transparent to spare implementation details
struct chef_config;

/**
 * @brief Loads the Chef configuration from the configured Chef directory.
 *
 * The configuration is read from `bake.json` below chef_dirs_config(). If the
 * file does not exist, a new configuration is created with platform defaults
 * (including the local CVD address on Linux). The returned configuration owns
 * its parsed data and is also the object used by the section and setter APIs.
 *
 * @return A newly loaded configuration, or NULL if the configuration directory
 *         cannot be resolved, memory allocation fails, or the file is invalid.
 */
extern struct chef_config* chef_config_load(void);

/**
 * @brief Writes a configuration back to its configured `bake.json` path.
 *
 * Address fields are serialized into the root JSON object before writing, and
 * sections modified through chef_config_section() or chef_config_set_string()
 * are persisted as part of the same JSON document. The configuration must
 * have been returned by chef_config_load().
 *
 * @param config Configuration to serialize and save.
 * @return 0 on success, or -1 if the configuration cannot be written.
 */
extern int chef_config_save(struct chef_config* config);

/**
 * @brief Returns a specific section from the configuration, if
 * the section does not exist, one will be created. The section is only
 * written when chef_config_save() is called.
 * 
 * @param config 
 */
extern void* chef_config_section(const struct chef_config* config, const char* section);

/**
 * @brief Returns a specific key from a specific section in the configuration.
 * If the key does not exist, NULL is returned. The returned string is owned
 * by the configuration object, and should not be freed by the caller.
 * 
 * @param config The configuration object
 * @param section Optional section, or NULL if the key is in the root
 * @param key The key to retrieve
 * @return const char* 
 */
extern const char* chef_config_get_string(const struct chef_config* config, void* section, const char* key);

/**
 * @brief Sets a specific key in a specific section in the configuration.
 * If the section does not exist, it will be created. The configuration
 * is only written when chef_config_save() is called.
 * 
 * @param config The configuration object
 * @param section Optional section, or NULL if the key is in the root
 * @param key The key to set
 * @param value The value to set, or NULL to remove the key
 * @return int 0 on success, -1 on failure
 */
extern int chef_config_set_string(struct chef_config* config, void* section, const char* key, const char* value);

/**
 * @brief 
 * 
 * @param address 
 */
extern void chef_config_cvd_address(struct chef_config* config, struct chef_config_address* address);
extern void chef_config_set_cvd_address(struct chef_config* config, struct chef_config_address* address);

/**
 * @brief 
 * 
 * @param address 
 */
extern void chef_config_remote_address(struct chef_config* config, struct chef_config_address* address);
extern void chef_config_set_remote_address(struct chef_config* config, struct chef_config_address* address);

#endif //!__CHEF_DIRS_H__
