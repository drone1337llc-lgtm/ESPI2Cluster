// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

#define WORKER_STATUS_HAS_DATA 0x01
#define WORKER_STATUS_WAITING 0x02

// CRC8 function declaration (C++ linkage)
uint8_t crc8_compute(const void *data, size_t len);
bool validatePacketCRC(const void *data, size_t len);

#pragma pack(push, 1)

struct PacketHello
{
    uint8_t cmd;
    uint8_t reserved;
    uint8_t requested_id;
    uint8_t crc;
};

struct PacketConfig
{
    uint8_t cmd;
    uint8_t assigned_id;
    uint8_t reserved;
    uint8_t crc;
};

struct PacketJob
{
    uint8_t cmd;
    uint8_t worker_id;
    uint8_t job_id;
    uint8_t reserved;
    float difficulty;
    uint32_t nonce_start;
    uint32_t nonce_range;
    uint8_t header[76];
    uint8_t crc;
};

struct PacketResult
{
    uint8_t cmd;
    uint8_t worker_id;
    uint8_t job_id;
    uint8_t reserved;
    uint32_t nonce;
    uint32_t hash_count;
    uint8_t crc;
};

struct PacketHeartbeat
{
    uint8_t cmd;
    uint8_t worker_id;
    uint8_t reserved;
    uint8_t crc;
};

#pragma pack(pop)

// Compile-time size verification
static_assert(sizeof(PacketHello) == 4, "PacketHello size mismatch");
static_assert(sizeof(PacketConfig) == 4, "PacketConfig size mismatch");
static_assert(sizeof(PacketJob) == 93, "PacketJob size mismatch");
static_assert(sizeof(PacketResult) == 13, "PacketResult size mismatch");
static_assert(sizeof(PacketHeartbeat) == 4, "PacketHeartbeat size mismatch");

#define SIZE_HELLO sizeof(PacketHello)
#define SIZE_CONFIG sizeof(PacketConfig)
#define SIZE_JOB sizeof(PacketJob)
#define SIZE_RESULT sizeof(PacketResult)
#define SIZE_HB sizeof(PacketHeartbeat)

#endif
