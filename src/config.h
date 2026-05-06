//Author : Sergio WIlliams
//GPL - 3.0 - or -later

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// DEVICE TYPE SELECTION (define ONE in platformio.ini)
// ============================================================================
// #define MASTER_DEVICE
// #define WORKER_DEVICE

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
// Set to 0 for production (web server provides monitoring instead)
#define DEBUG_ENABLED 1
#define DEBUG_SERIAL2 Serial2

#if DEBUG_ENABLED
#define DEBUG_PRINT(...) DEBUG_SERIAL2.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) DEBUG_SERIAL2.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) DEBUG_SERIAL2.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF(...)
#endif

// ============================================================================
// WIFI CONFIGURATION (Master only)
// ============================================================================
#define WIFI_SSID "Patricia27680"
#define WIFI_PASSWORD "FluffyBentley"
#define WEB_SERVER_PORT 80
#define WIFI_TIMEOUT_MS 30000
// ============================================================================
// MINING CONFIGURATION
// ============================================================================
#define POLL_INTERVAL_MS 200
#define HEARTBEAT_TIMEOUT_MS 15000
#define JOB_TIMEOUT_MS 150000
#define HEARTBEAT_TIMEOUT_MS 60000
#define MINING_STACK_SIZE 12288
#define DIFFICULTY_STEP 0.5f
#define MIN_DIFFICULTY 0.001f
#define MAX_DIFFICULTY 0.1f

#define NONCE_PER_JOB_SW 4096
#define NONCE_PER_JOB_HW 16384
#define MAX_NONCE_STEP 5000000U
#define MAX_NONCE 25000000U

#ifndef MAX_WORKERS
#define MAX_WORKERS 8
#endif

// ============================================================================
// LED CONFIGURATION
// ============================================================================
#define LED_PIN 48
#define NUMPIXELS 1
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#define LED_UPDATE_INTERVAL_MS 10
#define LED_BRIGHTNESS 50

// ============================================================================
// I2C CONFIGURATION
// ============================================================================
#ifndef I2C_MASTER_SDA
#define I2C_MASTER_SDA 8
#endif

#ifndef I2C_MASTER_SCL
#define I2C_MASTER_SCL 9
#endif

#ifndef I2C_SPEED
#define I2C_SPEED 400000
#endif

#ifndef TCA9548A_ADDR
#define TCA9548A_ADDR 0x70
#endif

#ifndef TCA9548A_RST_PIN
#define TCA9548A_RST_PIN 7
#endif

#ifndef WORKER_I2C_ADDR
#define WORKER_I2C_ADDR 0x20
#endif

// ============================================================================
// PROTOCOL COMMANDS
// ============================================================================
#define CMD_HELLO 0x01
#define CMD_ASSIGN_ID 0x02
#define CMD_JOB 0x03
#define CMD_RESULT 0x04
#define CMD_HEARTBEAT 0x05
#define CMD_ACK 0x06

#endif
