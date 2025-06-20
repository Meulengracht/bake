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
 * @brief 
 * 
 * @return int 
 */
extern struct chef_config* chef_config_load(const char* confdir);

/**
 * @brief 
 * 
 * @param config 
 * @return int 
 */
extern int chef_config_save(struct chef_config* config);

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
