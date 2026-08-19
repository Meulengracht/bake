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

#include <chef/vafs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#define CHEF_VAFS_ZSTD_CODEC_ID "zstd"
#define CHEF_VAFS_ZSTD_COMPRESSION_LEVEL 15

struct chef_vafs_codec_context {
    ZSTD_CCtx*       CCtx;
    ZSTD_DCtx*       DCtx;
    struct VaFsCodec Codec;
};

static int __chef_vafs_zstd_encode(
    const void* input,
    size_t      inputLength,
    void**      output,
    size_t*     outputLength,
    void*       userData)
{
    struct chef_vafs_codec_context* context = userData;
    size_t                          compressedCapacity;
    void*                           compressedData;
    size_t                          compressedSize;

    if (input == NULL || output == NULL || outputLength == NULL || context == NULL) {
        errno = EINVAL;
        return -1;
    }

    compressedCapacity = ZSTD_compressBound(inputLength);
    compressedData = malloc(compressedCapacity);
    if (compressedData == NULL) {
        errno = ENOMEM;
        return -1;
    }

    compressedSize = ZSTD_compressCCtx(
        context->CCtx,
        compressedData,
        compressedCapacity,
        input,
        inputLength,
        CHEF_VAFS_ZSTD_COMPRESSION_LEVEL
    );
    if (ZSTD_isError(compressedSize)) {
        free(compressedData);
        errno = EIO;
        return -1;
    }

    *output = compressedData;
    *outputLength = compressedSize;
    return 0;
}

static int __chef_vafs_zstd_decode(
    const void* input, 
    size_t      inputLength, 
    void*       output, 
    size_t      outputLength,
    void*       userData,
    size_t*     bytesWrittenOut)
{
    struct chef_vafs_codec_context* context = userData;
    size_t                          decompressedSize;

    if (input == NULL || output == NULL || bytesWrittenOut == NULL || context == NULL) {
        errno = EINVAL;
        return -1;
    }

    decompressedSize = ZSTD_decompressDCtx(context->DCtx, output, outputLength, input, inputLength);
    if (ZSTD_isError(decompressedSize)) {
        errno = EIO;
        return -1;
    }

    *bytesWrittenOut = decompressedSize;
    return 0;
}

int chef_vafs_codec_context_create(struct chef_vafs_codec_context** contextOut)
{
    struct chef_vafs_codec_context* context;

    if (contextOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    context = calloc(1, sizeof(struct chef_vafs_codec_context));
    if (context == NULL) {
        errno = ENOMEM;
        return -1;
    }

    context->CCtx = ZSTD_createCCtx();
    context->DCtx = ZSTD_createDCtx();
    if (context->CCtx == NULL || context->DCtx == NULL) {
        chef_vafs_codec_context_destroy(context);
        errno = ENOMEM;
        return -1;
    }

    context->Codec.ID = CHEF_VAFS_ZSTD_CODEC_ID;
    context->Codec.Encode = __chef_vafs_zstd_encode;
    context->Codec.Decode = __chef_vafs_zstd_decode;
    context->Codec.UserData = context;

    *contextOut = context;
    return 0;
}

void chef_vafs_codec_context_destroy(struct chef_vafs_codec_context* context)
{
    if (context == NULL) {
        return;
    }

    ZSTD_freeCCtx(context->CCtx);
    ZSTD_freeDCtx(context->DCtx);
    free(context);
}

void chef_vafs_builder_config_initialize(struct chef_vafs_codec_context* context, struct VaFsBuilderConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }

    vafs_builder_config_initialize(configuration);
    if (context == NULL) {
        return;
    }

    vafs_builder_config_set_codec(configuration, &context->Codec, 0);
    vafs_builder_config_set_codec(configuration, &context->Codec, 1);
}

int chef_vafs_builder_install_zstd_feature(struct VaFs* vafs)
{
    struct VaFsFeatureEncoding feature;

    memset(&feature, 0, sizeof(feature));
    memcpy(&feature.Header.Guid, &(struct VaFsGuid)VA_FS_FEATURE_FILTER, sizeof(struct VaFsGuid));
    feature.Header.Length = sizeof(struct VaFsFeatureEncoding);
    strncpy(feature.DescriptorEncoding, CHEF_VAFS_ZSTD_CODEC_ID, sizeof(feature.DescriptorEncoding) - 1);
    strncpy(feature.DataEncoding, CHEF_VAFS_ZSTD_CODEC_ID, sizeof(feature.DataEncoding) - 1);
    return vafs_builder_add_feature(vafs, &feature.Header);
}

void chef_vafs_reader_config_initialize(struct chef_vafs_codec_context* context, struct VaFsReaderConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }

    vafs_reader_config_initialize(configuration);
    if (context == NULL) {
        return;
    }

    vafs_reader_config_set_codecs(configuration, &context->Codec, 1);
}

int chef_vafs_reader_open_file(struct chef_vafs_codec_context* context, const char* path, struct VaFs** vafsOut)
{
    struct VaFsReaderConfiguration configuration;

    chef_vafs_reader_config_initialize(context, &configuration);
    return vafs_reader_open_file(path, &configuration, vafsOut);
}
