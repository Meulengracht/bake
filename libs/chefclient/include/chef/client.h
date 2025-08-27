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

#ifndef __LIBCHEF_CLIENT_H__
#define __LIBCHEF_CLIENT_H__

/**
 * @brief Initializes the chef client library and enables communication with the chef api
 * 
 * @return int returns -1 on error, 0 on success
 */
extern int chefclient_initialize(void);

/**
 * @brief Cleans up the chef client library. 
 * 
 */
extern void chefclient_cleanup(void);

enum chef_login_flow_type {
    CHEF_LOGIN_FLOW_TYPE_INVALID = 0,
    CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE,
    CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY
};

/**
 * @brief Parameters for the login flow.
 */
struct chefclient_login_params {
    enum chef_login_flow_type flow; /**< The type of login flow to use */
    const char* public_key;         /**< The public key to use for authentication */
    const char* private_key;        /**< The private key to use for authentication */
};

/**
 * @brief Initializes a new authentication session with the chef api. This is required
 * to use the 'publish' functionality. The rest of the methods are unprotected.
 * 
 * @return int 
 */
extern int chefclient_login(struct chefclient_login_params* params);

/**
 * @brief Terminates the current authentication session with the chef api.
 * 
 */
extern void chefclient_logout(void);

/**
 * @brief Generates a new RSA keypair and saves it to the specified directory.
 * 
 * @param bits The number of bits for the RSA key. Minimum is 2048.
 * @param directory The directory to save the keypair to. This should be the .chef directory in the user's home directory.
 * @param publicKeyPath Output parameter that will contain the path to the generated public key file. This should be freed by the caller.
 * @param privateKeyPath Output parameter that will contain the path to the generated private key file. This should be freed by the caller.
 * @return int Returns -1 on error, 0 on success.
 */
extern int pubkey_generate_rsa_keypair(int bits, const char* directory, char** publicKeyPath, char** privateKeyPath);

#endif //!__LIBCHEF_CLIENT_H__
