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

#ifndef __LIBCHEF_API_ACCOUNT_H__
#define __LIBCHEF_API_ACCOUNT_H__

#include <stddef.h>

struct chef_account;

enum chef_account_status {
    CHEF_ACCOUNT_STATUS_UNKNOWN,
    CHEF_ACCOUNT_STATUS_ACTIVE,
    CHEF_ACCOUNT_STATUS_LOCKED,
    CHEF_ACCOUNT_STATUS_DELETED
};

enum chef_account_verified_status {
    CHEF_ACCOUNT_VERIFIED_STATUS_UNKNOWN,
    CHEF_ACCOUNT_VERIFIED_STATUS_PENDING,
    CHEF_ACCOUNT_VERIFIED_STATUS_REJECTED,
    CHEF_ACCOUNT_VERIFIED_STATUS_VERIFIED,
};

struct chef_publisher;

/**
 * @brief Creates a new account instance. This is neccessary if no account exists yet.
 * However the account is not saved to the server until chef_account_update is called.
 */
extern struct chef_account* chef_account_new(void);

/**
 * @brief Retrieves the account information of the current user. This requires
 * that @chefclient_login has been called.
 * 
 * @param[In] accountOut A pointer where to store the allocated account instance.
 * @return int -1 on error, 0 on success. Errno will be set accordingly.
 */
extern int chef_account_get(struct chef_account** accountOut);

/**
 * @brief Updates the account information on the server. This requires that
 * @chefclient_login has been called.
 * 
 * @param[In] account A pointer to the account instance that will be updated.
 * @return int -1 on error, 0 on success. Errno will be set accordingly.
 */
extern int chef_account_update(struct chef_account* account);

/**
 * @brief Retrieves information about a specific publisher.
 * 
 * @param[In] publisher    The name of the publisher to retrieve information about.
 * @param[In] publisherOut A pointer where to store the allocated publisher instance.
 * @return int -1 on error, 0 on success. Errno will be set accordingly.
 */
extern int chef_account_publisher_get(const char* publisher, struct chef_publisher** publisherOut);

extern int chef_account_publisher_register(const char* name, const char* email);

extern int chef_account_apikey_create(const char* name, char** apiKey);
extern int chef_account_apikey_delete(const char* name);


/**
 * @brief The publisher API. The structure will be allocated by chef_account_publisher_get
 * and must be freed with the below free call.
 */
extern void chef_publisher_free(struct chef_publisher* publisher);

/**
 * @brief The account API. Any structures that are allocated by chef_account_new, 
 * chef_account_get must be freed with the below free call. This will free any sub-resources
 * that was created as a part of these calls, and there is no reason to free anything else.
 */
extern void chef_account_free(struct chef_account* account);

extern const char*              chef_account_name(struct chef_account* account);
extern const char*              chef_account_email(struct chef_account* account);
extern enum chef_account_status chef_account_status(struct chef_account* account);

extern void chef_account_name_set(struct chef_account* account, const char* name);

extern int                    chef_account_publisher_count(struct chef_account* account);
extern struct chef_publisher* chef_account_publisher(struct chef_account* account, int index);


extern const char*                       chef_publisher_name(struct chef_publisher* publisher);
extern const char*                       chef_publisher_email(struct chef_publisher* publisher);
extern const char*                       chef_publisher_public_key(struct chef_publisher* publisher);
extern enum chef_account_verified_status chef_publisher_verified_status(struct chef_publisher* publisher);

extern int         chef_account_apikey_count(struct chef_account* account);
extern const char* chef_account_apikey_name(struct chef_account* account, int index);

#endif //!__LIBCHEF_API_ACCOUNT_H__
