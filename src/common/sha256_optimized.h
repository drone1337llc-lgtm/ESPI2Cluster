// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#ifndef SHA256_OPTIMIZED_H
#define SHA256_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

struct SHA256Context
{
    uint8_t buffer[64];
    uint32_t digest[8];
};

// CRC8 function (C++ linkage)
uint8_t crc8_compute(const void *data, size_t len);
bool validatePacketCRC(const void *data, size_t len);

#ifdef __cplusplus
extern "C"
{
#endif

    // Optimized SHA256 functions (nerdSHA256plus style)
    IRAM_ATTR void sha256_midstate(uint32_t *digest, const uint8_t *data);
    IRAM_ATTR void sha256_bake(const uint32_t *digest, const uint8_t *data, uint32_t *bake);
    IRAM_ATTR bool sha256_double_baked(const uint32_t *digest, const uint8_t *data,
                                       const uint32_t *bake, uint8_t *hash, float difficulty);

// Hardware SHA256 (ESP32-S3)
#ifdef HARDWARE_SHA256
    IRAM_ATTR void sha256_hw_init();
    IRAM_ATTR void sha256_hw_midstate(uint32_t *digest, const uint8_t *data);
    IRAM_ATTR bool sha256_hw_double_baked(const uint32_t *digest, const uint8_t *data,
                                          const uint32_t *bake, uint8_t *hash, float difficulty);
#endif

#ifdef __cplusplus
}
#endif

#endif
