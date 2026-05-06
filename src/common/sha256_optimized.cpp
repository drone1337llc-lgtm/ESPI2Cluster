//Author : Sergio WIlliams
//GPL - 3.0 - or -later

#include "sha256_optimized.h"
#include <string.h>
#include <esp_system.h>

#ifdef HARDWARE_SHA256
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>
#endif

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define GET_UINT32_BE(data, offset)                                            \
    (((uint32_t)(data)[offset] << 24) | ((uint32_t)(data)[offset + 1] << 16) | \
     ((uint32_t)(data)[offset + 2] << 8) | ((uint32_t)(data)[offset + 3]))

#define PUT_UINT32_BE(n, data, offset)           \
    {                                            \
        (data)[offset] = ((n) >> 24) & 0xFF;     \
        (data)[offset + 1] = ((n) >> 16) & 0xFF; \
        (data)[offset + 2] = ((n) >> 8) & 0xFF;  \
        (data)[offset + 3] = (n) & 0xFF;         \
    }

#define S0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))
#define S2(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define F0(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define F1(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

#define P(a, b, c, d, e, f, g, h, x, k)                \
    {                                                  \
        temp1 = (h) + S3(e) + F1(e, f, g) + (k) + (x); \
        temp2 = S2(a) + F0(a, b, c);                   \
        (d) += temp1;                                  \
        (h) = temp1 + temp2;                           \
    }

static const uint32_t K[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

IRAM_ATTR void sha256_midstate(uint32_t *digest, const uint8_t *data)
{
    uint32_t A[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                     0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
    uint32_t W[64], temp1, temp2;

    for (int i = 0; i < 16; i++)
    {
        W[i] = GET_UINT32_BE(data, i * 4);
    }

    // Unrolled first 16 rounds
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[2], K[2]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[3], K[3]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[4], K[4]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[5], K[5]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[6], K[6]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[7], K[7]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[8], K[8]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[9], K[9]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[10], K[10]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[11], K[11]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[12], K[12]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[13], K[13]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[14], K[14]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[15], K[15]);

    // Remaining 48 rounds
    for (int i = 16; i < 64; i++)
    {
        W[i] = S1(W[i - 2]) + W[i - 7] + S0(W[i - 15]) + W[i - 16];
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        A[7] = A[6];
        A[6] = A[5];
        A[5] = A[4];
        A[4] = A[3];
        A[3] = A[2];
        A[2] = A[1];
        A[1] = A[0];
        A[0] = tmp;
    }

    // Store final state (midstate)
    for (int i = 0; i < 8; i++)
    {
        digest[i] = A[i];
    }
}

IRAM_ATTR void sha256_bake(const uint32_t *digest, const uint8_t *data, uint32_t *bake)
{
    uint32_t A[8], temp1, temp2;

    bake[0] = GET_UINT32_BE(data, 0);
    bake[1] = GET_UINT32_BE(data, 4);
    bake[2] = GET_UINT32_BE(data, 8);

    // Pre-calculate W[3] and W[4] for nonce insertion point
    // 640 = 512 (first block bits) + 128 (second block header bits)
    bake[3] = S1(bake[2]) + bake[1] + S0(bake[1]) + bake[0];
    bake[4] = S1(bake[3]) + bake[2] + S0(bake[2]) + bake[1];

    for (int i = 0; i < 8; i++)
        A[i] = digest[i];

    // Only 3 rounds needed for bake
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], bake[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], bake[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], bake[2], K[2]);

    // Store intermediate values for later rounds
    temp1 = A[7] + S3(A[4]) + F1(A[4], A[5], A[6]) + K[3];
    bake[13] = temp1;
    bake[14] = S2(A[0]) + F0(A[0], A[1], A[2]);

    for (int i = 0; i < 8; i++)
    {
        bake[5 + i] = A[i];
    }
}

IRAM_ATTR bool sha256_double_baked(const uint32_t *digest, const uint8_t *data,
                                   const uint32_t *bake, uint8_t *hash, float difficulty)
{
    uint32_t A[8], W[64], temp1, temp2;

    W[0] = bake[0];
    W[1] = bake[1];
    W[2] = bake[2];
    W[3] = GET_UINT32_BE(data, 12);
    W[4] = 0x80000000;
    for (int i = 5; i < 15; i++)
        W[i] = 0;
    W[15] = 640;
    W[16] = bake[3];
    W[17] = bake[4];

    for (int i = 0; i < 8; i++)
        A[i] = bake[5 + i];

    // Use pre-calculated values from bake
    temp1 = bake[13] + W[3];
    temp2 = bake[14];
    A[3] += temp1;
    A[7] = temp1 + temp2;

    // Continue from round 4
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[4], K[4]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[5], K[5]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[6], K[6]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[7], K[7]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[8], K[8]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[9], K[9]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[10], K[10]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[11], K[11]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[12], K[12]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[13], K[13]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[14], K[14]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[15], K[15]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[16], K[16]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[17], K[17]);

    for (int i = 18; i < 64; i++)
    {
        W[i] = S1(W[i - 2]) + W[i - 7] + S0(W[i - 15]) + W[i - 16];
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        A[7] = A[6];
        A[6] = A[5];
        A[5] = A[4];
        A[4] = A[3];
        A[3] = A[2];
        A[2] = A[1];
        A[1] = A[0];
        A[0] = tmp;
    }

    // Second SHA256 - store first hash result in W
    W[0] = A[0] + digest[0];
    W[1] = A[1] + digest[1];
    W[2] = A[2] + digest[2];
    W[3] = A[3] + digest[3];
    W[4] = A[4] + digest[4];
    W[5] = A[5] + digest[5];
    W[6] = A[6] + digest[6];
    W[7] = A[7] + digest[7];
    W[8] = 0x80000000;
    for (int i = 9; i < 15; i++)
        W[i] = 0;
    W[15] = 256;

    A[0] = 0x6A09E667;
    A[1] = 0xBB67AE85;
    A[2] = 0x3C6EF372;
    A[3] = 0xA54FF53A;
    A[4] = 0x510E527F;
    A[5] = 0x9B05688C;
    A[6] = 0x1F83D9AB;
    A[7] = 0x5BE0CD19;

    for (int i = 0; i < 64; i++)
    {
        if (i > 15)
        {
            W[i] = S1(W[i - 2]) + W[i - 7] + S0(W[i - 15]) + W[i - 16];
        }
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        A[7] = A[6];
        A[6] = A[5];
        A[5] = A[4];
        A[4] = A[3];
        A[3] = A[2];
        A[2] = A[1];
        A[1] = A[0];
        A[0] = tmp;
    }

    // Output final hash
    for (int i = 0; i < 8; i++)
    {
        uint32_t val = A[i] + ((i == 0) ? 0x6A09E667 : (i == 1) ? 0xBB67AE85
                                                   : (i == 2)   ? 0x3C6EF372
                                                   : (i == 3)   ? 0xA54FF53A
                                                   : (i == 4)   ? 0x510E527F
                                                   : (i == 5)   ? 0x9B05688C
                                                   : (i == 6)   ? 0x1F83D9AB
                                                                : 0x5BE0CD19);
        PUT_UINT32_BE(val, hash, i * 4);
    }

    // Early termination check - compare FIRST 4 bytes (most significant)
    // This is correct for Bitcoin-style difficulty (hash must be LESS than target)
    uint32_t hash_prefix = GET_UINT32_BE(hash, 0);
    uint32_t target = (uint32_t)(0xFFFFFFFFUL / difficulty);

    return (hash_prefix < target);
}

#ifdef HARDWARE_SHA256
IRAM_ATTR void sha256_hw_init()
{
    esp_sha_acquire_hardware();
}

IRAM_ATTR void sha256_hw_midstate(uint32_t *digest, const uint8_t *data)
{
    sha_hal_hash_block(SHA2_256, (const uint32_t *)data, 16, true);
    sha_hal_read_digest(SHA2_256, digest);
}

IRAM_ATTR bool sha256_hw_double_baked(const uint32_t *digest, const uint8_t *data,
                                      const uint32_t *bake, uint8_t *hash, float difficulty)
{
    // Use hardware SHA for first round
    uint32_t hw_digest[8];
    memcpy(hw_digest, digest, sizeof(hw_digest));

    // Prepare block with nonce
    uint32_t block[16];
    memcpy(block, data, 64);

    esp_sha_acquire_hardware();
    REG_WRITE(SHA_MODE_REG, SHA2_256);
    sha_hal_write_digest(SHA2_256, hw_digest);
    sha_hal_hash_block(SHA2_256, block, 16, true);
    sha_hal_read_digest(SHA2_256, hw_digest);
    esp_sha_release_hardware();

    // Software second round
    uint32_t W[64], A[8], temp1, temp2;

    for (int i = 0; i < 8; i++)
    {
        W[i] = hw_digest[i];
    }
    W[8] = 0x80000000;
    for (int i = 9; i < 15; i++)
        W[i] = 0;
    W[15] = 256;

    A[0] = 0x6A09E667;
    A[1] = 0xBB67AE85;
    A[2] = 0x3C6EF372;
    A[3] = 0xA54FF53A;
    A[4] = 0x510E527F;
    A[5] = 0x9B05688C;
    A[6] = 0x1F83D9AB;
    A[7] = 0x5BE0CD19;

    for (int i = 0; i < 64; i++)
    {
        if (i > 15)
        {
            W[i] = S1(W[i - 2]) + W[i - 7] + S0(W[i - 15]) + W[i - 16];
        }
        P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[i], K[i]);
        uint32_t tmp = A[7];
        A[7] = A[6];
        A[6] = A[5];
        A[5] = A[4];
        A[4] = A[3];
        A[3] = A[2];
        A[2] = A[1];
        A[1] = A[0];
        A[0] = tmp;
    }

    for (int i = 0; i < 8; i++)
    {
        uint32_t val = A[i] + ((i == 0) ? 0x6A09E667 : (i == 1) ? 0xBB67AE85
                                                   : (i == 2)   ? 0x3C6EF372
                                                   : (i == 3)   ? 0xA54FF53A
                                                   : (i == 4)   ? 0x510E527F
                                                   : (i == 5)   ? 0x9B05688C
                                                   : (i == 6)   ? 0x1F83D9AB
                                                                : 0x5BE0CD19);
        PUT_UINT32_BE(val, hash, i * 4);
    }

    uint32_t hash_prefix = GET_UINT32_BE(hash, 0);
    uint32_t target = (uint32_t)(0xFFFFFFFFUL / difficulty);

    return (hash_prefix < target);
}
#endif
