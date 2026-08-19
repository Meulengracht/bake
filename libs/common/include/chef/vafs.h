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

#ifndef __COMMON_CHEF_VAFS_H__
#define __COMMON_CHEF_VAFS_H__

#include <stdint.h>
#include <stddef.h>
#include <vafs/builder.h>
#include <vafs/reader.h>

/**
 * @brief Chef-specific VaFs integration helpers.
 *
 * This header centralizes the codec and feature wiring that Chef expects when
 * it creates or opens `.pack` images through the newer VaFs builder/reader
 * API. Callers use these helpers instead of open-coding reader or builder
 * configuration so the project keeps one consistent compression policy.
 */

/**
 * @brief Reusable ZSTD state behind Chef's codec callbacks.
 *
 * A context must stay alive for as long as any VaFs handle configured with it
 * remains open, since VaFs copies the codec descriptor (including this
 * context as its `UserData`) into the image's stream state.
 */
struct chef_vafs_codec_context;

/**
 * @brief Allocates a codec context for use with the helpers below.
 *
 * @param[Out] contextOut Receives the new codec context.
 * @return 0 on success, -1 on failure with errno set.
 */
extern int chef_vafs_codec_context_create(struct chef_vafs_codec_context** contextOut);

/**
 * @brief Releases a codec context created by chef_vafs_codec_context_create().
 *
 * Only call this once every VaFs handle configured with the context has been
 * closed. Passing NULL is a no-op.
 */
extern void chef_vafs_codec_context_destroy(struct chef_vafs_codec_context* context);

/**
 * @brief Initializes a builder configuration with Chef defaults.
 *
 * This starts from VaFs library defaults and then registers Chef's supported
 * codec policy for both descriptor and data streams, backed by `context`.
 *
 * @param[In]  context       Codec context that must outlive the resulting image handle.
 * @param[Out] configuration Builder configuration to initialize.
 */
extern void chef_vafs_builder_config_initialize(struct chef_vafs_codec_context* context, struct VaFsBuilderConfiguration* configuration);

/**
 * @brief Persists Chef's encoding feature on an open VaFs builder handle.
 *
 * Chef package images store the codec ids they were written with as a VaFs
 * feature so readers can resolve the matching decode callbacks later. Call
 * this after `vafs_builder_new()` and before finalizing the image.
 *
 * @param[In] vafs Open VaFs builder handle.
 * @return 0 on success, -1 on failure with errno set.
 */
extern int chef_vafs_builder_install_zstd_feature(struct VaFs* vafs);

/**
 * @brief Initializes a reader configuration with Chef's supported codecs.
 *
 * This prepares a VaFs reader configuration that can open Chef package images
 * written with the project's expected compression policy, backed by `context`.
 *
 * @param[In]  context       Codec context that must outlive the resulting image handle.
 * @param[Out] configuration Reader configuration to initialize.
 */
extern void chef_vafs_reader_config_initialize(struct chef_vafs_codec_context* context, struct VaFsReaderConfiguration* configuration);

/**
 * @brief Opens a VaFs image file using Chef's reader configuration defaults.
 *
 * This is a convenience wrapper for callers that only need the standard Chef
 * codec registry and do not need to customize the underlying VaFs reader
 * configuration.
 *
 * @param[In]  context Codec context that must outlive the returned VaFs handle.
 * @param[In]  path    Path to the VaFs image to open.
 * @param[Out] vafsOut Receives the opened VaFs reader handle.
 * @return 0 on success, -1 on failure with errno set.
 */
extern int chef_vafs_reader_open_file(struct chef_vafs_codec_context* context, const char* path, struct VaFs** vafsOut);

#endif //!__COMMON_CHEF_VAFS_H__
