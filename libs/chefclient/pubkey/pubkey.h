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

#ifndef __LIBCHEF_PUBKEY_H__
#define __LIBCHEF_PUBKEY_H__

/** 
 * @brief Attempts to login using the public key authentication flow.
 * @param publicKey The public key to use for authentication.
 * @param privateKey The private key to use for authentication. This is only used initially
 * to generate the session key.
 * @return int returns -1 on error, 0 on success
 */
extern int pubkey_login(const char* publicKey, const char* privateKey);

/**
 * @brief Logs out the current user and clears the authentication context.
 */
extern void pubkey_logout(void);

/**
 * @brief Sets the authentication headers for the current user.
 * @param headerlist The list of headers to set the authentication headers on.
 */
extern void pubkey_set_authentication(void** headerlist);

#endif //!__LIBCHEF_PUBKEY_H__