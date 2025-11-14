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

/**
 * @brief Account status enumeration.
 */
enum chef_account_status {
    CHEF_ACCOUNT_STATUS_UNKNOWN, /**< Unknown status */
    CHEF_ACCOUNT_STATUS_ACTIVE,  /**< Account is active and can be used */
    CHEF_ACCOUNT_STATUS_LOCKED,  /**< Account is locked and cannot be used */
    CHEF_ACCOUNT_STATUS_DELETED  /**< Account has been deleted */
};

/**
 * @brief Publisher verification status enumeration.
 */
enum chef_account_verified_status {
    CHEF_ACCOUNT_VERIFIED_STATUS_UNKNOWN,  /**< Unknown verification status */
    CHEF_ACCOUNT_VERIFIED_STATUS_PENDING,  /**< Verification is pending */
    CHEF_ACCOUNT_VERIFIED_STATUS_REJECTED, /**< Verification was rejected */
    CHEF_ACCOUNT_VERIFIED_STATUS_VERIFIED, /**< Publisher is verified */
};

struct chef_publisher;

/**
 * @brief Creates a new account instance. This is necessary if no account exists yet.
 * However the account is not saved to the server until chef_account_update is called.
 * 
 * @return struct chef_account* Returns a pointer to the newly allocated account instance,
 *                              or NULL on error. Must be freed with chef_account_free.
 */
extern struct chef_account* chef_account_new(void);

/**
 * @brief Retrieves the account information of the current user. This requires
 * that chefclient_login has been called.
 * 
 * @param[Out] accountOut A pointer where to store the allocated account instance
 * @return int            Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_get(struct chef_account** accountOut);

/**
 * @brief Updates the account information on the server. This requires that
 * chefclient_login has been called.
 * 
 * @param[In] account A pointer to the account instance that will be updated
 * @return int        Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_update(struct chef_account* account);

/**
 * @brief Retrieves information about a specific publisher.
 * 
 * @param[In]  publisher    The name of the publisher to retrieve information about
 * @param[Out] publisherOut A pointer where to store the allocated publisher instance
 * @return int              Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_publisher_get(const char* publisher, struct chef_publisher** publisherOut);

/**
 * @brief Registers a new publisher account.
 * 
 * This function creates a new publisher registration request. Authentication is required.
 * 
 * @param[In] name  The desired publisher name
 * @param[In] email The publisher's contact email address
 * @return int      Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_publisher_register(const char* name, const char* email);

/**
 * @brief Creates a new API key for authentication.
 * 
 * This function generates a new API key that can be used for programmatic access.
 * Authentication is required.
 * 
 * @param[In]  name   The name/label for the API key
 * @param[Out] apiKey A pointer where the generated API key string will be stored. Must be freed by caller.
 * @return int        Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_apikey_create(const char* name, char** apiKey);

/**
 * @brief Deletes an existing API key.
 * 
 * This function revokes and removes an API key. Authentication is required.
 * 
 * @param[In] name The name/label of the API key to delete
 * @return int     Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_account_apikey_delete(const char* name);


/**
 * @brief Frees a publisher structure.
 * 
 * The structure will be allocated by chef_account_publisher_get
 * and must be freed with this function.
 * 
 * @param[In] publisher The publisher instance to free
 */
extern void chef_publisher_free(struct chef_publisher* publisher);

/**
 * @brief Frees an account structure.
 * 
 * Any structures that are allocated by chef_account_new or chef_account_get
 * must be freed with this function. This will free any sub-resources
 * that were created as a part of these calls.
 * 
 * @param[In] account The account instance to free
 */
extern void chef_account_free(struct chef_account* account);

/**
 * @brief Gets the account name.
 * 
 * @param[In] account The account instance
 * @return const char* The account name, or NULL if not set
 */
extern const char*              chef_account_name(struct chef_account* account);

/**
 * @brief Gets the account email address.
 * 
 * @param[In] account The account instance
 * @return const char* The account email, or NULL if not set
 */
extern const char*              chef_account_email(struct chef_account* account);

/**
 * @brief Gets the account status.
 * 
 * @param[In] account The account instance
 * @return enum chef_account_status The current status of the account
 */
extern enum chef_account_status chef_account_status(struct chef_account* account);

/**
 * @brief Sets the account name.
 * 
 * @param[In] account The account instance to modify
 * @param[In] name    The new name for the account
 */
extern void chef_account_name_set(struct chef_account* account, const char* name);

/**
 * @brief Gets the number of publishers associated with this account.
 * 
 * @param[In] account The account instance
 * @return int The number of publishers, or 0 if none
 */
extern int                    chef_account_publisher_count(struct chef_account* account);

/**
 * @brief Gets a publisher from the account by index.
 * 
 * @param[In] account The account instance
 * @param[In] index   The index of the publisher (0-based)
 * @return struct chef_publisher* The publisher at the specified index, or NULL if invalid
 */
extern struct chef_publisher* chef_account_publisher(struct chef_account* account, int index);

/**
 * @brief Gets the publisher name.
 * 
 * @param[In] publisher The publisher instance
 * @return const char* The publisher name, or NULL if not set
 */
extern const char*                       chef_publisher_name(struct chef_publisher* publisher);

/**
 * @brief Gets the publisher email address.
 * 
 * @param[In] publisher The publisher instance
 * @return const char* The publisher email, or NULL if not set
 */
extern const char*                       chef_publisher_email(struct chef_publisher* publisher);

/**
 * @brief Gets the publisher's public key.
 * 
 * @param[In] publisher The publisher instance
 * @return const char* The public key string, or NULL if not set
 */
extern const char*                       chef_publisher_public_key(struct chef_publisher* publisher);

/**
 * @brief Gets the publisher's signed key.
 * 
 * @param[In] publisher The publisher instance
 * @return const char* The signed key string, or NULL if not set
 */
extern const char*                       chef_publisher_signed_key(struct chef_publisher* publisher);

/**
 * @brief Gets the publisher's verification status.
 * 
 * @param[In] publisher The publisher instance
 * @return enum chef_account_verified_status The current verification status
 */
extern enum chef_account_verified_status chef_publisher_verified_status(struct chef_publisher* publisher);

/**
 * @brief Gets the number of API keys associated with this account.
 * 
 * @param[In] account The account instance
 * @return int The number of API keys, or 0 if none
 */
extern int         chef_account_apikey_count(struct chef_account* account);

/**
 * @brief Gets an API key name from the account by index.
 * 
 * @param[In] account The account instance
 * @param[In] index   The index of the API key (0-based)
 * @return const char* The API key name at the specified index, or NULL if invalid
 */
extern const char* chef_account_apikey_name(struct chef_account* account, int index);

#endif //!__LIBCHEF_API_ACCOUNT_H__
