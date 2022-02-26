/**
 * @file decode.c
 * @author Philip Meulengracht (the_meulengracht@hotmail.com)
 * @brief A base64 decoder taken from StackOverflow at this link https://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c 
 * @version 1.0
 * @date 2022-02-22
 * 
 * No copyright will be provided here as this is modified from stack overflow
 */

#include "base64.h"
#include <stdlib.h>
#include <string.h>

static const int g_b64Table[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,
    0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

unsigned char* base64_decode(const unsigned char* data, size_t len)
{
    const unsigned char* p   = data;
    int                  pad = len > 0 && (len % 4 || p[len - 1] == '=');
    const size_t         L   = ((len + 3) / 4 - pad) * 4;
    unsigned char*       decodedData;
    const size_t         decodedSize = ((L / 4) * 3) + pad + 1;
    size_t               i, j;
    
    decodedData = malloc(decodedSize);
    if (!decodedData) {
        return NULL;
    }
    memset(decodedData, 0, decodedSize);

    for (i = 0, j = 0; i < L; i += 4) {
        int n = g_b64Table[p[i]] << 18 | g_b64Table[p[i + 1]] << 12 | g_b64Table[p[i + 2]] << 6 | g_b64Table[p[i + 3]];
        decodedData[j++] = n >> 16;
        decodedData[j++] = n >> 8 & 0xFF;
        decodedData[j++] = n & 0xFF;
    }

    if (pad) {
        int n = g_b64Table[p[L]] << 18 | g_b64Table[p[L + 1]] << 12;
        decodedData[decodedSize - 2] = n >> 16;

        if (len > L + 2 && p[L + 2] != '=') {
            n |= g_b64Table[p[L + 2]] << 6;
            decodedData[decodedSize - 1] = (n >> 8) & 0xFF;
        }
    }
    return decodedData;
}
