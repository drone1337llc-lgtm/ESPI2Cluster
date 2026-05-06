//Author : Sergio WIlliams
//GPL - 3.0 - or -later

#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include <new>

       // LED Status Patterns - RED, GREEN, or BLUE ONLY
       // Use flash/fade effects for additional status differentiation
       enum LedPattern {
           LED_OFF = 0,
           LED_STARTUP,        // GREEN fading in/out (boot sequence)
           LED_CONNECTING,     // BLUE breathing (searching for master)
           LED_REGISTERED,     // Solid BLUE (registered with master)
           LED_MINING_IDLE,    // BLUE fast fade (ready but no job)
           LED_MINING_ACTIVE,  // Solid GREEN (mining normally)
           LED_SHARE_FOUND,    // GREEN rapid flash (share submitted)
           LED_SHARE_ACCEPTED, // GREEN breathing (share accepted)
           LED_SHARE_REJECTED, // RED rapid flash (share rejected)
           LED_ERROR,          // RED fast blink (critical error)
           LED_NO_JOB,         // BLUE slow blink (no job received)
           LED_NO_HEARTBEAT    // RED slow blink (connection lost)
       };

class LedManager
{
public:
    static LedManager &getInstance();

    void begin();
    void setPattern(LedPattern pattern);
    LedPattern getPattern() const { return m_current_pattern; }
    void update();
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setBrightness(uint8_t brightness);
    ~LedManager();

private:
    LedManager();
    LedManager(const LedManager &) = delete;
    LedManager &operator=(const LedManager &) = delete;

    Adafruit_NeoPixel *m_pixels;
    LedPattern m_current_pattern;
    uint32_t m_last_update;
    uint32_t m_pattern_timer;
    uint8_t m_brightness;
    uint8_t m_current_r, m_current_g, m_current_b;
    bool m_blink_state;
    uint8_t m_flash_count;

    void setLedRGB(uint8_t r, uint8_t g, uint8_t b);
    void updatePattern(LedPattern pattern, uint32_t now);
    void breatheEffect(uint8_t baseR, uint8_t baseG, uint8_t baseB, uint32_t now);
    void blinkEffect(uint8_t onR, uint8_t onG, uint8_t onB,
                     uint8_t offR, uint8_t offG, uint8_t offB,
                     uint32_t interval, uint32_t now);
    void flashEffect(uint8_t flashR, uint8_t flashG, uint8_t flashB,
                     uint8_t baseR, uint8_t baseG, uint8_t baseB, uint32_t now);
    void fadeEffect(uint32_t now, bool fadeIn);
    void rainbowEffect(uint32_t now);
    bool isPatternComplete(LedPattern pattern, uint32_t now);
};

#define LED LedManager::getInstance()

#endif
