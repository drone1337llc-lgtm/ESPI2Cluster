//Author : Sergio WIlliams
//GPL - 3.0 - or -later

#ifdef MASTER_DEVICE

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "common/protocol.h"
#include "common/sha256_optimized.h"
#include "common/led_manager.h"
#include "master/i2c_mux.h"

// ============================================================================
// CONSTANTS
// ============================================================================
#define I2C_BUFFER_SIZE 256
#define HASHRATE_AVG_SAMPLES 30
#define HASHRATE_DECAY_MS 180000
#define RX2_PIN 2
#define TX2_PIN 1

// ============================================================================
// WORKER STRUCTURE
// ============================================================================
struct WorkerData
{
    uint8_t id;
    bool active;
    uint32_t last_seen;
    uint32_t shares_submitted;
    uint32_t shares_accepted;
    float hashrate;
    float hashrate_avg;
    uint32_t hashes_processed;
    uint32_t last_hash_count;
    uint32_t last_job_sent;
    uint8_t last_job_id;
    uint32_t last_hash_time;
    uint32_t hashrate_samples;
    bool pending_config;
    uint32_t config_time;
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
static WorkerData g_workers[MAX_WORKERS];
static uint8_t g_active_worker_count = 0;
static uint8_t g_job_id = 0;
static uint8_t g_block_header[76];
static uint32_t g_total_submitted = 0;
static uint32_t g_total_accepted = 0;
static float g_worker_difficulty = 0.01f;
static LedPattern g_current_led_pattern = LED_OFF;
static uint32_t g_led_pattern_time = 0;

static uint8_t g_rx_buffer[I2C_BUFFER_SIZE];

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void processWorkerData(uint8_t channel);
void sendJobToWorker(uint8_t channel, uint8_t worker_id);
void cleanupStaleWorkers();
void updateHashrate();
void printStatus();
int findWorkerSlot();
void updateLEDStatus();
void scanI2CBus();
bool readWorkerData(uint8_t channel, uint8_t *buffer, uint8_t *len);
float calculateTotalHashrate();

// ============================================================================
// SETUP
// ============================================================================
void setup()
{
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(2000);
    Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t seed = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3];
    seed += millis();
    randomSeed(seed);

    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("ESP32 MINING MASTER");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF("Max Workers: %d\n", MAX_WORKERS);
    DEBUG_PRINTF("I2C Speed: %d Hz\n", I2C_SPEED);

    LED.begin();
    LED.setPattern(LED_STARTUP);
    g_current_led_pattern = LED_STARTUP;
    g_led_pattern_time = millis();

    if (!g_i2c_mux.begin())
    {
        DEBUG_PRINTLN("[I2C] Mux initialization failed");
    }

    scanI2CBus();

    memset(g_workers, 0, sizeof(g_workers));
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        g_workers[i].id = 0xFF;
    }

    DEBUG_PRINTLN("READY - Waiting for workers...");
    DEBUG_PRINTLN("========================================");

    
    delay(100);
}

// ============================================================================
// SCAN I2C BUS
// ============================================================================
void scanI2CBus()
{
    DEBUG_PRINTLN("[I2C] Scanning bus...");

    if (g_i2c_mux.isMuxPresent())
    {
        for (uint8_t ch = 0; ch < 8; ch++)
        {
            if (g_i2c_mux.selectChannel(ch))
            {
                uint8_t count = 0;
                for (uint8_t addr = 1; addr < 127; addr++)
                {
                    Wire.beginTransmission(addr);
                    if (Wire.endTransmission() == 0)
                    {
                        count++;
                    }
                }
                g_i2c_mux.deselectChannel(ch);

                if (count > 0)
                {
                    DEBUG_PRINTF("Channel %d: %d device(s)\n", ch, count);
                }
            }
        }
    }

    
}

// ============================================================================
// LOOP
// ============================================================================
void loop()
{
    static uint32_t last_poll = 0;
    static uint32_t last_status = 0;
    static uint32_t last_job = 0;
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        processWorkerData(i);
    }

    if (now - last_poll >= POLL_INTERVAL_MS)
    {
        last_poll = now;
        cleanupStaleWorkers();
        updateHashrate();
        updateLEDStatus();
        LED.update();
    }

    if (now - last_job >= 5000 && g_active_worker_count > 0)
    {
        last_job = now;
        g_job_id++;

        for (int i = 0; i < 76; i++)
        {
            g_block_header[i] = random(0, 255);
        }

        for (uint8_t i = 0; i < MAX_WORKERS; i++)
        {
            if (g_workers[i].active)
            {
                sendJobToWorker(i, i);
            }
        }

        DEBUG_PRINTF("Job %d sent\n", g_job_id);
    }

    if (now - last_status >= 10000)
    {
        last_status = now;
        printStatus();
        
    }

    delay(10);
}

// ============================================================================
// CALCULATE TOTAL HASHRATE
// ============================================================================
float calculateTotalHashrate()
{
    float total = 0;
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        if (g_workers[i].active)
        {
            total += g_workers[i].hashrate;
        }
    }
    return total;
}

// ============================================================================
// UPDATE LED STATUS
// ============================================================================
void updateLEDStatus()
{
    uint32_t now = millis();

    if (now - g_led_pattern_time < 200)
        return;

    LedPattern target_pattern = LED_OFF;

    if (g_active_worker_count == 0)
    {
        target_pattern = LED_CONNECTING;
    }
    else if (g_total_submitted > 0 && g_total_accepted < g_total_submitted)
    {
        target_pattern = LED_MINING_IDLE;
    }
    else if (g_active_worker_count > 0)
    {
        target_pattern = LED_MINING_ACTIVE;
    }

    if (target_pattern != g_current_led_pattern)
    {
        LED.setPattern(target_pattern);
        g_current_led_pattern = target_pattern;
        g_led_pattern_time = now;
    }
}

// ============================================================================
// READ WORKER DATA
// ============================================================================
bool readWorkerData(uint8_t channel, uint8_t *buffer, uint8_t *len)
{
    if (!g_i2c_mux.selectChannel(channel))
        return false;

    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(100);
    Wire.requestFrom(WORKER_I2C_ADDR, 1);

    if (!Wire.available())
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    uint8_t status = Wire.read();

    if (!(status & WORKER_STATUS_HAS_DATA))
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(100);
    Wire.requestFrom(WORKER_I2C_ADDR, 1);

    if (!Wire.available())
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    uint8_t dataLen = Wire.read();

    if (dataLen == 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(100);
    Wire.requestFrom(WORKER_I2C_ADDR, (int)dataLen);

    uint8_t received = 0;
    while (Wire.available() && received < dataLen)
    {
        buffer[received++] = Wire.read();
    }

    g_i2c_mux.deselectChannel(channel);

    *len = dataLen;
    return (received == dataLen && dataLen >= 4);
}

// ============================================================================
// PROCESS WORKER DATA
// ============================================================================
void processWorkerData(uint8_t channel)
{
    if (g_workers[channel].pending_config)
    {
        uint32_t now = millis();
        if (now - g_workers[channel].config_time >= 100)
        {
            if (!g_i2c_mux.selectChannel(channel))
                return;

            PacketConfig cfg;
            cfg.cmd = CMD_ASSIGN_ID;
            cfg.assigned_id = channel;
            cfg.reserved = 0;
            cfg.crc = crc8_compute(&cfg, sizeof(PacketConfig) - 1);

            Wire.beginTransmission(WORKER_I2C_ADDR);
            Wire.write((uint8_t *)&cfg, sizeof(PacketConfig));
            uint8_t result = Wire.endTransmission();

            if (result == 0)
            {
                g_workers[channel].pending_config = false;
            }

            g_i2c_mux.deselectChannel(channel);
            delayMicroseconds(100);
        }
        return;
    }

    uint8_t len = 0;
    if (!readWorkerData(channel, g_rx_buffer, &len))
        return;

    uint8_t cmd = g_rx_buffer[0];

    if (cmd == CMD_HELLO && len >= SIZE_HELLO && validatePacketCRC(g_rx_buffer, len))
    {
        int slot = findWorkerSlot();
        if (slot >= 0)
        {
            g_workers[slot].id = slot;
            g_workers[slot].active = true;
            g_workers[slot].last_seen = millis();
            g_workers[slot].last_hash_time = millis();
            g_workers[slot].pending_config = true;
            g_workers[slot].config_time = millis();
            g_active_worker_count++;
            DEBUG_PRINTF("Worker %d connected\n", slot);
        }
    }
    else if (cmd == CMD_RESULT && len >= SIZE_RESULT && validatePacketCRC(g_rx_buffer, len))
    {
        PacketResult *p = (PacketResult *)g_rx_buffer;
        uint8_t wid = p->worker_id;

        if (wid < MAX_WORKERS && g_workers[wid].active)
        {
            g_workers[wid].last_seen = millis();
            g_workers[wid].hashes_processed = p->hash_count;
            g_workers[wid].last_job_sent = 0;

            if (p->nonce != 0xFFFFFFFF)
            {
                g_total_submitted++;
                g_workers[wid].shares_submitted++;

                if (random(100) < 90)
                {
                    g_total_accepted++;
                    g_workers[wid].shares_accepted++;
                    DEBUG_PRINTF("Share accepted! W:%d N:%08X\n", wid, p->nonce);
                }
                else
                {
                    DEBUG_PRINTF("Share rejected! W:%d N:%08X\n", wid, p->nonce);
                }
            }
        }
    }
}

// ============================================================================
// SEND JOB TO WORKER
// ============================================================================
void sendJobToWorker(uint8_t channel, uint8_t worker_id)
{
    if (worker_id >= MAX_WORKERS || !g_i2c_mux.selectChannel(channel))
        return;

    PacketJob job;
    job.cmd = CMD_JOB;
    job.worker_id = worker_id;
    job.job_id = g_job_id;
    job.reserved = 0;
    job.difficulty = g_worker_difficulty;
    job.nonce_start = 0;
    job.nonce_range = MAX_NONCE_STEP;
    memcpy(job.header, g_block_header, 76);
    job.crc = crc8_compute(&job, sizeof(PacketJob) - 1);

    Wire.beginTransmission(WORKER_I2C_ADDR);
    Wire.write(sizeof(PacketJob));
    Wire.write((uint8_t *)&job, sizeof(PacketJob));
    Wire.endTransmission();

    g_workers[worker_id].last_job_sent = millis();
    g_workers[worker_id].last_job_id = g_job_id;

    g_i2c_mux.deselectChannel(channel);
}

// ============================================================================
// CLEANUP STALE WORKERS
// ============================================================================
void cleanupStaleWorkers()
{
    uint32_t now = millis();
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        if (g_workers[i].id == 0xFF)
            continue;
        if (!g_workers[i].active)
            continue;

        // FIX: Increased timeout
        if (now - g_workers[i].last_seen > (HEARTBEAT_TIMEOUT_MS * 2))
        {
            DEBUG_PRINTF("Worker %d timeout\n", i);
            g_workers[i].active = false;
            g_workers[i].id = 0xFF;
            g_active_worker_count--;
        }
    }
}

// ============================================================================
// UPDATE HASHRATE
// ============================================================================
void updateHashrate()
{
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        if (!g_workers[i].active)
            continue;

        uint32_t delta = g_workers[i].hashes_processed - g_workers[i].last_hash_count;

        if (delta > 0)
        {
            g_workers[i].last_hash_time = now;
            float instant = (float)delta * (1000.0f / POLL_INTERVAL_MS);

            if (g_workers[i].hashrate_samples < HASHRATE_AVG_SAMPLES)
            {
                g_workers[i].hashrate_samples++;
                g_workers[i].hashrate_avg =
                    (g_workers[i].hashrate_avg * (g_workers[i].hashrate_samples - 1) + instant) / g_workers[i].hashrate_samples;
            }
            else
            {
                g_workers[i].hashrate_avg =
                    (g_workers[i].hashrate_avg * (HASHRATE_AVG_SAMPLES - 1) + instant) / HASHRATE_AVG_SAMPLES;
            }
            g_workers[i].hashrate = g_workers[i].hashrate_avg;
        }

        if (now - g_workers[i].last_hash_time > HASHRATE_DECAY_MS)
        {
            g_workers[i].hashrate_avg *= 0.9f;
            g_workers[i].hashrate = g_workers[i].hashrate_avg;
        }

        g_workers[i].last_hash_count = g_workers[i].hashes_processed;
    }
}

// ============================================================================
// FIND WORKER SLOT
// ============================================================================
int findWorkerSlot()
{
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        if (g_workers[i].id == 0xFF)
            return i;
    }
    return -1;
}

// ============================================================================
// PRINT STATUS
// ============================================================================
void printStatus()
{
    DEBUG_PRINTF("Workers: %d/%d, Hashrate: %.1f H/s, Shares: %d/%d\n",
                 g_active_worker_count, MAX_WORKERS,
                 calculateTotalHashrate(), g_total_accepted, g_total_submitted);
}

#endif // MASTER_DEVICE
