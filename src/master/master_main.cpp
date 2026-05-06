// Author : Sergio WIlliams
// GPL - 3.0 - or -later

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
    uint8_t recovery_attempts;
    uint32_t last_recovery_attempt;
    bool needs_health_check;
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

// Job assignment tracking for staggered distribution
static uint8_t g_last_job_worker = 0xFF;
static uint32_t g_last_job_time = 0;
static bool g_job_pending = false;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void processWorkerData(uint8_t channel);
bool sendJobToWorker(uint8_t channel, uint8_t worker_id);
void cleanupStaleWorkers();
void updateHashrate();
void printStatus();
int findWorkerSlot();
void updateLEDStatus();
void scanI2CBus();
bool readWorkerData(uint8_t channel, uint8_t *buffer, uint8_t *len);
float calculateTotalHashrate();
bool checkWorkerHealth(uint8_t channel);
bool recoverWorker(uint8_t channel);
void scheduleNextJob();
void processJobAssignment();
void formatHashrate(float hashrate, char *buffer, size_t bufferSize);

// ============================================================================
// FORMAT HASHRATE (User-friendly units)
// ============================================================================
void formatHashrate(float hashrate, char *buffer, size_t bufferSize)
{
    const char *units[] = {"H/s", "KH/s", "MH/s", "GH/s", "TH/s"};
    int unitIndex = 0;
    float value = hashrate;

    // Scale to appropriate unit
    while (value >= 1000.0f && unitIndex < 4)
    {
        value /= 1000.0f;
        unitIndex++;
    }

    // Format with appropriate precision
    if (value >= 100.0f)
    {
        snprintf(buffer, bufferSize, "%.1f %s", value, units[unitIndex]);
    }
    else if (value >= 10.0f)
    {
        snprintf(buffer, bufferSize, "%.2f %s", value, units[unitIndex]);
    }
    else
    {
        snprintf(buffer, bufferSize, "%.3f %s", value, units[unitIndex]);
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup()
{
    // MAXIMIZE SERIAL BUFFERS
    Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
    Serial.setTxBufferSize(SERIAL_TX_BUFFER_SIZE);
    Serial.begin(115200);
    delay(2000);

    Serial2.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
    Serial2.setTxBufferSize(SERIAL_TX_BUFFER_SIZE);
    Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t seed = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3];
    seed += millis();
    randomSeed(seed);

    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("ESP32 MINING MASTER - OPTIMIZED");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF("Max Workers: %d\n", MAX_WORKERS);
    DEBUG_PRINTF("I2C Speed: %d Hz\n", I2C_SPEED);
    DEBUG_PRINTF("I2C Buffer: %d bytes\n", I2C_BUFFER_SIZE);
    DEBUG_PRINTF("Serial Buffer: %d bytes\n", SERIAL_RX_BUFFER_SIZE);
    DEBUG_PRINTF("Job Delay: %d ms\n", JOB_ASSIGNMENT_DELAY_MS);

    LED.begin();
    LED.setPattern(LED_STARTUP);
    g_current_led_pattern = LED_STARTUP;
    g_led_pattern_time = millis();

    // Initialize I2C with maximum buffer
    Wire.setBufferSize(I2C_BUFFER_SIZE);

    if (!g_i2c_mux.begin())
    {
        DEBUG_PRINTLN("[I2C] Mux initialization failed");
    }

    scanI2CBus();

    memset(g_workers, 0, sizeof(g_workers));
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        g_workers[i].id = 0xFF;
        g_workers[i].recovery_attempts = 0;
        g_workers[i].needs_health_check = true;
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
                    //DEBUG_PRINTF("Channel %d: %d device(s)\n", ch, count);
                }
            }
        }
    }
}

// ============================================================================
// CHECK WORKER HEALTH
// ============================================================================
bool checkWorkerHealth(uint8_t channel)
{
    if (!g_i2c_mux.selectChannel(channel))
        return false;

    // Quick I2C ping
    Wire.beginTransmission(WORKER_I2C_ADDR);
    uint8_t result = Wire.endTransmission();

    g_i2c_mux.deselectChannel(channel);

    return (result == 0);
}

// ============================================================================
// RECOVER WORKER
// ============================================================================
bool recoverWorker(uint8_t channel)
{
    if (g_workers[channel].recovery_attempts >= WORKER_RECOVERY_ATTEMPTS)
    {
        //DEBUG_PRINTF("Worker %d: Max recovery attempts reached\n", channel);
        return false;
    }

    uint32_t now = millis();
    if (now - g_workers[channel].last_recovery_attempt < WORKER_RECOVERY_INTERVAL_MS)
        return false;

    g_workers[channel].last_recovery_attempt = now;
    g_workers[channel].recovery_attempts++;

    //DEBUG_PRINTF("Worker %d: Recovery attempt %d/%d\n",
                 //channel, g_workers[channel].recovery_attempts, WORKER_RECOVERY_ATTEMPTS);

    // Reset I2C channel
    g_i2c_mux.deselectChannel(channel);
    delayMicroseconds(100);

    if (!g_i2c_mux.selectChannel(channel))
        return false;

    // Send config packet to re-register
    PacketConfig cfg;
    cfg.cmd = CMD_ASSIGN_ID;
    cfg.assigned_id = channel;
    cfg.reserved = 0;
    cfg.crc = crc8_compute(&cfg, sizeof(PacketConfig) - 1);

    Wire.beginTransmission(WORKER_I2C_ADDR);
    Wire.write((uint8_t *)&cfg, sizeof(PacketConfig));
    uint8_t result = Wire.endTransmission();

    g_i2c_mux.deselectChannel(channel);

    if (result == 0)
    {
        g_workers[channel].pending_config = true;
        g_workers[channel].config_time = now;
        //DEBUG_PRINTF("Worker %d: Recovery config sent\n", channel);
        return true;
    }

    return false;
}

// ============================================================================
// SCHEDULE NEXT JOB
// ============================================================================
void scheduleNextJob()
{
    // Find next active worker in sequence
    uint8_t start_worker = (g_last_job_worker + 1) % MAX_WORKERS;

    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        uint8_t worker = (start_worker + i) % MAX_WORKERS;

        if (g_workers[worker].active)
        {
            g_last_job_worker = worker;
            g_job_pending = true;
            return;
        }
    }

    g_job_pending = false;
}

// ============================================================================
// PROCESS JOB ASSIGNMENT (Staggered)
// ============================================================================
void processJobAssignment()
{
    if (!g_job_pending)
        return;

    uint32_t now = millis();

    // Check if enough time has passed since last job
    if (now - g_last_job_time < JOB_ASSIGNMENT_DELAY_MS)
        return;

    uint8_t worker = g_last_job_worker;

// Health check before sending job
#if WORKER_HEALTH_CHECK_BEFORE_JOB
    if (!checkWorkerHealth(worker))
    {
        //DEBUG_PRINTF("Worker %d: Health check failed, attempting recovery\n", worker);

        if (!recoverWorker(worker))
        {
            // Mark worker as inactive if recovery fails
            g_workers[worker].active = false;
            g_workers[worker].id = 0xFF;
            g_active_worker_count--;
            //DEBUG_PRINTF("Worker %d: Marked inactive\n", worker);
        }

        g_job_pending = false;
        scheduleNextJob();
        return;
    }
#endif

    // Send job to worker
    if (sendJobToWorker(worker, worker))
    {
        g_last_job_time = now;
        g_job_pending = false;
        //DEBUG_PRINTF("Job %d sent to Worker %d\n", g_job_id, worker);

        // Schedule next worker
        scheduleNextJob();
    }
    else
    {
        //DEBUG_PRINTF("Worker %d: Job send failed\n", worker);
        g_job_pending = false;
        scheduleNextJob();
    }
}

// ============================================================================
// LOOP
// ============================================================================
void loop()
{
    static uint32_t last_poll = 0;
    static uint32_t last_status = 0;
    static uint32_t last_job_trigger = 0;
    uint32_t now = millis();

    // Process all workers for incoming data
    for (uint8_t i = 0; i < MAX_WORKERS; i++)
    {
        processWorkerData(i);
    }

    // Periodic maintenance
    if (now - last_poll >= POLL_INTERVAL_MS)
    {
        last_poll = now;
        cleanupStaleWorkers();
        updateHashrate();
        updateLEDStatus();
        LED.update();
    }

    // Trigger new job cycle every 5 seconds
    if (now - last_job_trigger >= 5000 && g_active_worker_count > 0)
    {
        last_job_trigger = now;
        g_job_id++;

        // Generate new block header
        for (int i = 0; i < 76; i++)
        {
            g_block_header[i] = random(0, 255);
        }

        // Start staggered job assignment
        scheduleNextJob();

        DEBUG_PRINTF("Job %d cycle started\n", g_job_id);
    }

    // Process staggered job assignments
    processJobAssignment();

    // Status print - NOW EVERY 1 SECOND (was 10 seconds)
    if (now - last_status >= 1000)
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
// READ WORKER DATA (Optimized)
// ============================================================================
bool readWorkerData(uint8_t channel, uint8_t *buffer, uint8_t *len)
{
    if (!g_i2c_mux.selectChannel(channel))
        return false;

    // Single transaction to get status
    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(50);
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

    // Get data length
    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(50);
    Wire.requestFrom(WORKER_I2C_ADDR, 1);

    if (!Wire.available())
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    uint8_t dataLen = Wire.read();

    if (dataLen == 0 || dataLen > I2C_BUFFER_SIZE)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    // Read data
    Wire.beginTransmission(WORKER_I2C_ADDR);
    if (Wire.endTransmission() != 0)
    {
        g_i2c_mux.deselectChannel(channel);
        return false;
    }

    delayMicroseconds(50);
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
            delayMicroseconds(50);
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
            g_workers[slot].recovery_attempts = 0;
            g_workers[slot].needs_health_check = false;
            g_active_worker_count++;
            //DEBUG_PRINTF("Worker %d connected\n", slot);
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
            g_workers[wid].recovery_attempts = 0; // Reset on successful communication

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
// SEND JOB TO WORKER (Optimized)
// ============================================================================
bool sendJobToWorker(uint8_t channel, uint8_t worker_id)
{
    if (worker_id >= MAX_WORKERS)
        return false;

    if (!g_i2c_mux.selectChannel(channel))
        return false;

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

    // Single optimized transmission
    Wire.beginTransmission(WORKER_I2C_ADDR);
    Wire.write(sizeof(PacketJob));
    Wire.write((uint8_t *)&job, sizeof(PacketJob));
    uint8_t result = Wire.endTransmission();

    g_i2c_mux.deselectChannel(channel);

    if (result == 0)
    {
        g_workers[worker_id].last_job_sent = millis();
        g_workers[worker_id].last_job_id = g_job_id;
        return true;
    }

    return false;
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

        // Attempt recovery before marking dead
        if (now - g_workers[i].last_seen > HEARTBEAT_TIMEOUT_MS)
        {
            if (g_workers[i].recovery_attempts < WORKER_RECOVERY_ATTEMPTS)
            {
                recoverWorker(i);
            }
            else
            {
                //DEBUG_PRINTF("Worker %d: Timeout after %d recovery attempts\n",
                             //i, g_workers[i].recovery_attempts);
                g_workers[i].active = false;
                g_workers[i].id = 0xFF;
                g_active_worker_count--;
            }
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
    char hashrateStr[32];
    formatHashrate(calculateTotalHashrate(), hashrateStr, sizeof(hashrateStr));

    DEBUG_PRINTF("Workers: %d/%d, Hashrate: %s, Shares: %d/%d, Job Delay: %dms\n",
                 g_active_worker_count, MAX_WORKERS,
                 hashrateStr, g_total_accepted, g_total_submitted,
                 JOB_ASSIGNMENT_DELAY_MS);
}

#endif // MASTER_DEVICE
