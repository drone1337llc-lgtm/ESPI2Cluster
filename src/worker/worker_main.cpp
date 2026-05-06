// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#ifdef WORKER_DEVICE

#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "common/protocol.h"
#include "common/sha256_optimized.h"
#include "common/led_manager.h"

#define I2C_SLAVE_ADDR WORKER_I2C_ADDR

enum I2CState
{
    I2C_STATE_IDLE,
    I2C_STATE_STATUS_REQUESTED,
    I2C_STATE_LENGTH_REQUESTED,
    I2C_STATE_DATA_REQUESTED
};

static volatile uint32_t g_hashes_computed = 0;
static volatile uint32_t g_found_nonce = 0xFFFFFFFF;
static volatile bool g_has_job = false;
static volatile float g_current_difficulty = 0.01f;
static uint8_t g_job_header[80];
static uint32_t g_job_midstate[8];
static uint32_t g_job_bake[15];
static uint32_t g_nonce_current = 0;
static uint32_t g_nonce_end = 0;
static uint8_t g_worker_id = 0xFF;
static bool g_is_registered = false;

static volatile uint32_t g_hw_nonce_start = 0;
static volatile uint32_t g_hw_nonce_end = 0;
static volatile uint32_t g_sw_nonce_start = 0;
static volatile uint32_t g_sw_nonce_end = 0;
static volatile bool g_hw_has_job = false;
static volatile bool g_sw_has_job = false;

static uint8_t g_tx_buffer[I2C_BUFFER_SIZE];
static uint8_t g_tx_len = 0;
static volatile bool g_tx_pending = false;
static volatile bool g_data_ready = false;

static volatile I2CState g_i2c_state = I2C_STATE_IDLE;
static uint8_t g_status_byte = 0;

static uint32_t g_last_hello_time = 0;
static uint8_t g_hello_retry_count = 0;
#define HELLO_RETRY_INTERVAL_MS 1000
#define HELLO_MAX_RETRIES 10

static uint32_t g_last_result_time = 0;
#define RESULT_REPORT_INTERVAL_MS 2000

static uint32_t g_last_communication_time = 0;
#define COMMUNICATION_TIMEOUT_MS 30000

static portMUX_TYPE g_hash_mutex = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_job_mutex = portMUX_INITIALIZER_UNLOCKED;

void miningTaskHW(void *pvParameters);
void miningTaskSW(void *pvParameters);
void handleIncomingPacket(uint8_t *data, uint8_t len);
void prepareTransmission(uint8_t *data, uint8_t len);
void sendHelloPacket();
void updateStatusByte();
void sendResultPacket();
void checkCommunicationTimeout();

void updateStatusByte()
{
    g_status_byte = 0;

    if (g_data_ready && g_tx_len > 0)
    {
        g_status_byte |= WORKER_STATUS_HAS_DATA;
    }
    else
    {
        g_status_byte |= WORKER_STATUS_WAITING;
    }
}

void onRequest()
{
    updateStatusByte();
    g_last_communication_time = millis();

    switch (g_i2c_state)
    {
    case I2C_STATE_IDLE:
        Wire.write(g_status_byte);
        g_i2c_state = I2C_STATE_STATUS_REQUESTED;
        break;

    case I2C_STATE_STATUS_REQUESTED:
        if (g_data_ready && g_tx_len > 0)
        {
            Wire.write(g_tx_len);
            g_i2c_state = I2C_STATE_LENGTH_REQUESTED;
        }
        else
        {
            Wire.write(0);
            g_i2c_state = I2C_STATE_IDLE;
        }
        break;

    case I2C_STATE_LENGTH_REQUESTED:
        if (g_data_ready && g_tx_len > 0)
        {
            Wire.write(g_tx_buffer, g_tx_len);
            g_data_ready = false;
            g_tx_pending = false;
        }
        else
        {
            Wire.write(0);
        }
        g_i2c_state = I2C_STATE_IDLE;
        break;

    default:
        Wire.write(0);
        g_i2c_state = I2C_STATE_IDLE;
        break;
    }
}

void onReceive(int len)
{
    static uint8_t rx_buffer[I2C_BUFFER_SIZE];
    int rx_len = 0;

    g_last_communication_time = millis();

    while (Wire.available() && rx_len < I2C_BUFFER_SIZE)
    {
        rx_buffer[rx_len++] = Wire.read();
    }

    if (rx_len > 0)
    {
        if (rx_len > 1 && rx_buffer[0] == (rx_len - 1))
        {
            handleIncomingPacket(rx_buffer + 1, rx_len - 1);
        }
        else
        {
            handleIncomingPacket(rx_buffer, rx_len);
        }
    }

    g_i2c_state = I2C_STATE_IDLE;
}

void sendHelloPacket()
{
    if (g_is_registered)
        return;

    PacketHello hello;
    hello.cmd = CMD_HELLO;
    hello.reserved = 0;
    hello.requested_id = 0;
    hello.crc = crc8_compute(&hello, sizeof(PacketHello) - 1);
    prepareTransmission((uint8_t *)&hello, sizeof(PacketHello));

    g_last_hello_time = millis();
    g_hello_retry_count++;

    DEBUG_PRINTF("Hello sent (attempt %d)\n", g_hello_retry_count);
}

void sendResultPacket()
{
    if (!g_is_registered)
        return;

    taskENTER_CRITICAL(&g_hash_mutex);
    uint32_t hashes = g_hashes_computed;
    uint32_t nonce = g_found_nonce;
    g_hashes_computed = 0;
    g_found_nonce = 0xFFFFFFFF;
    taskEXIT_CRITICAL(&g_hash_mutex);

    if (hashes > 0 || nonce != 0xFFFFFFFF)
    {
        PacketResult res;
        res.cmd = CMD_RESULT;
        res.worker_id = g_worker_id;
        res.job_id = 0;
        res.reserved = 0;
        res.nonce = nonce;
        res.hash_count = hashes;
        res.crc = crc8_compute(&res, sizeof(PacketResult) - 1);
        prepareTransmission((uint8_t *)&res, sizeof(PacketResult));

        DEBUG_PRINTF("Result sent: hashes=%u, nonce=%08X\n", hashes, nonce);
    }
}

void checkCommunicationTimeout()
{
    uint32_t now = millis();

    if (g_is_registered)
    {
        if (now - g_last_communication_time > COMMUNICATION_TIMEOUT_MS)
        {
            DEBUG_PRINTLN("[WORKER] Communication timeout - re-registering");
            g_is_registered = false;
            g_hello_retry_count = 0;
            LED.setPattern(LED_CONNECTING);
        }
    }
}

void setup()
{
    // MAXIMIZE SERIAL BUFFERS
    Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
    Serial.setTxBufferSize(SERIAL_TX_BUFFER_SIZE);
    Serial.begin(115200);
    delay(2000);

    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("ESP32 MINING WORKER - OPTIMIZED");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF("I2C Address: 0x%02X\n", I2C_SLAVE_ADDR);
    DEBUG_PRINTF("I2C Buffer: %d bytes\n", I2C_BUFFER_SIZE);
    DEBUG_PRINTF("Serial Buffer: %d bytes\n", SERIAL_RX_BUFFER_SIZE);

    // Initialize I2C with maximum buffer
    Wire.setBufferSize(I2C_BUFFER_SIZE);
    Wire.begin(I2C_SLAVE_ADDR);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);

    DEBUG_PRINTLN("[I2C] Slave initialized");

    LED.begin();
    LED.setPattern(LED_STARTUP);

#ifdef HARDWARE_SHA256
    sha256_hw_init();
    DEBUG_PRINTLN("[SHA] Hardware initialized");
#endif

    esp_task_wdt_init(10, false);

    xTaskCreatePinnedToCore(miningTaskHW, "MinerHW", MINING_STACK_SIZE, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(miningTaskSW, "MinerSW", MINING_STACK_SIZE, NULL, 4, NULL, 1);

    g_last_communication_time = millis();
    sendHelloPacket();

    DEBUG_PRINTLN("Waiting for master assignment...");

    Serial.flush();
    delay(100);
}

void loop()
{
    uint32_t now = millis();

    if (!g_is_registered)
    {
        if (now - g_last_hello_time >= HELLO_RETRY_INTERVAL_MS)
        {
            if (g_hello_retry_count < HELLO_MAX_RETRIES)
            {
                sendHelloPacket();
            }
            else
            {
                DEBUG_PRINTLN("[WORKER] Max hello retries - check master");
                LED.setPattern(LED_ERROR);
                g_hello_retry_count = 0;
                g_last_hello_time = now;
            }
        }
    }
    else
    {
        if (now - g_last_result_time >= RESULT_REPORT_INTERVAL_MS)
        {
            g_last_result_time = now;
            sendResultPacket();
        }

        // Check for communication timeout
        checkCommunicationTimeout();
    }

    LED.update();
    delay(10);
}

void handleIncomingPacket(uint8_t *data, uint8_t len)
{
    if (len < 4)
        return;

    uint8_t cmd = data[0];

    if (cmd == CMD_ASSIGN_ID && len >= sizeof(PacketConfig))
    {
        if (validatePacketCRC(data, len))
        {
            PacketConfig *cfg = (PacketConfig *)data;
            g_worker_id = cfg->assigned_id;
            g_is_registered = true;
            g_hello_retry_count = 0;
            g_last_communication_time = millis();

            DEBUG_PRINTF("Assigned ID: %d\n", g_worker_id);
            DEBUG_PRINTLN("[WORKER] Registered!");
            LED.setPattern(LED_REGISTERED);
        }
    }
    else if (cmd == CMD_JOB && len >= sizeof(PacketJob))
    {
        if (validatePacketCRC(data, len))
        {
            PacketJob *job = (PacketJob *)data;

            if (!g_is_registered)
            {
                DEBUG_PRINTLN("[WORKER] Rejecting job - not registered");
                return;
            }

            taskENTER_CRITICAL(&g_job_mutex);

            memcpy(g_job_header, job->header, 76);

            sha256_midstate(g_job_midstate, g_job_header);
            sha256_bake(g_job_midstate, g_job_header + 64, g_job_bake);

            uint32_t total_range = job->nonce_range;
            uint32_t hw_range = (total_range * 2) / 3;
            uint32_t sw_range = total_range - hw_range;

            g_hw_nonce_start = job->nonce_start;
            g_hw_nonce_end = g_hw_nonce_start + hw_range;
            g_hw_has_job = true;

            g_sw_nonce_start = g_hw_nonce_end;
            g_sw_nonce_end = g_sw_nonce_start + sw_range;
            g_sw_has_job = true;

            g_nonce_current = job->nonce_start;
            g_nonce_end = g_nonce_current + job->nonce_range;
            g_current_difficulty = job->difficulty;
            g_has_job = true;

            taskEXIT_CRITICAL(&g_job_mutex);

            DEBUG_PRINTF("Job %d received\n", job->job_id);
            LED.setPattern(LED_MINING_ACTIVE);

            g_last_result_time = millis();
            g_last_communication_time = millis();
        }
    }
}

void prepareTransmission(uint8_t *data, uint8_t len)
{
    if (len > 0 && len <= I2C_BUFFER_SIZE)
    {
        memcpy(g_tx_buffer, data, len);
        g_tx_len = len;
        g_tx_pending = true;
        g_data_ready = true;
        updateStatusByte();
    }
}

void miningTaskHW(void *pvParameters)
{
    uint8_t hash[32];
    uint32_t wdt_counter = 0;
    uint32_t local_hashes = 0;

    DEBUG_PRINTLN("[MINER] HW task started on Core 0");

    while (1)
    {
        if (g_hw_has_job)
        {
            taskENTER_CRITICAL(&g_job_mutex);
            uint32_t batch_end = g_hw_nonce_start + NONCE_PER_JOB_HW;
            uint32_t job_end = g_hw_nonce_end;
            bool has_job = g_hw_has_job;
            taskEXIT_CRITICAL(&g_job_mutex);

            if (batch_end > job_end)
                batch_end = job_end;

            for (uint32_t n = g_hw_nonce_start; n < batch_end; n++)
            {
                g_job_header[72] = n & 0xFF;
                g_job_header[73] = (n >> 8) & 0xFF;
                g_job_header[74] = (n >> 16) & 0xFF;
                g_job_header[75] = (n >> 24) & 0xFF;

#ifdef HARDWARE_SHA256
                if (sha256_hw_double_baked(g_job_midstate, g_job_header + 64, g_job_bake, hash, g_current_difficulty))
#else
                if (sha256_double_baked(g_job_midstate, g_job_header + 64, g_job_bake, hash, g_current_difficulty))
#endif
                {
                    g_found_nonce = n;
                    LED.setPattern(LED_SHARE_FOUND);
                }

                local_hashes++;

                if ((n & 0xFF) == 0)
                {
                    taskENTER_CRITICAL(&g_job_mutex);
                    has_job = g_hw_has_job;
                    taskEXIT_CRITICAL(&g_job_mutex);

                    if (!has_job)
                        break;
                }
            }

            taskENTER_CRITICAL(&g_job_mutex);
            g_hw_nonce_start = batch_end;

            if (g_hw_nonce_start >= g_hw_nonce_end)
            {
                g_hw_has_job = false;
            }
            taskEXIT_CRITICAL(&g_job_mutex);

            taskENTER_CRITICAL(&g_hash_mutex);
            g_hashes_computed += local_hashes;
            local_hashes = 0;
            taskEXIT_CRITICAL(&g_hash_mutex);
        }
        else
        {
            vTaskDelay(1);
        }

        wdt_counter++;
        if (wdt_counter >= 100)
        {
            wdt_counter = 0;
            esp_task_wdt_reset();
        }
    }
}

void miningTaskSW(void *pvParameters)
{
    uint8_t hash[32];
    uint32_t wdt_counter = 0;
    uint32_t local_hashes = 0;

    DEBUG_PRINTLN("[MINER] SW task started on Core 1");

    while (1)
    {
        if (g_sw_has_job)
        {
            taskENTER_CRITICAL(&g_job_mutex);
            uint32_t batch_end = g_sw_nonce_start + NONCE_PER_JOB_SW;
            uint32_t job_end = g_sw_nonce_end;
            bool has_job = g_sw_has_job;
            taskEXIT_CRITICAL(&g_job_mutex);

            if (batch_end > job_end)
                batch_end = job_end;

            for (uint32_t n = g_sw_nonce_start; n < batch_end; n++)
            {
                g_job_header[72] = n & 0xFF;
                g_job_header[73] = (n >> 8) & 0xFF;
                g_job_header[74] = (n >> 16) & 0xFF;
                g_job_header[75] = (n >> 24) & 0xFF;

                if (sha256_double_baked(g_job_midstate, g_job_header + 64, g_job_bake, hash, g_current_difficulty))
                {
                    g_found_nonce = n;
                    LED.setPattern(LED_SHARE_FOUND);
                }

                local_hashes++;

                if ((n & 0xFF) == 0)
                {
                    taskENTER_CRITICAL(&g_job_mutex);
                    has_job = g_sw_has_job;
                    taskEXIT_CRITICAL(&g_job_mutex);

                    if (!has_job)
                        break;
                }
            }

            taskENTER_CRITICAL(&g_job_mutex);
            g_sw_nonce_start = batch_end;

            if (g_sw_nonce_start >= g_sw_nonce_end)
            {
                g_sw_has_job = false;
            }
            taskEXIT_CRITICAL(&g_job_mutex);

            taskENTER_CRITICAL(&g_hash_mutex);
            g_hashes_computed += local_hashes;
            local_hashes = 0;
            taskEXIT_CRITICAL(&g_hash_mutex);
        }
        else
        {
            vTaskDelay(1);
        }

        wdt_counter++;
        if (wdt_counter >= 100)
        {
            wdt_counter = 0;
            esp_task_wdt_reset();
        }
    }
}

#endif // WORKER_DEVICE
