// Author : Sergio WIlliams
// GPL - 3.0 - or -later
#include "led_manager.h"

LedManager &LedManager::getInstance()
{
    static LedManager instance;
    return instance;
}

LedManager::LedManager()
    : m_pixels(nullptr), m_current_pattern(LED_OFF), m_last_update(0),
      m_pattern_timer(0), m_brightness(LED_BRIGHTNESS),
      m_current_r(0), m_current_g(0), m_current_b(0),
      m_blink_state(false), m_flash_count(0)
{
}

LedManager::~LedManager()
{
    if (m_pixels)
    {
        delete m_pixels;
        m_pixels = nullptr;
    }
}

void LedManager::begin()
{
    if (m_pixels)
    {
        delete m_pixels;
    }

    m_pixels = new (std::nothrow) Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEOPIXEL_TYPE);

    if (!m_pixels)
    {
        DEBUG_PRINTLN("[LED] ERROR: NeoPixel allocation failed");
        return;
    }

    m_pixels->begin();
    m_pixels->setBrightness(m_brightness);
    m_pixels->clear();
    m_pixels->show();
    DEBUG_PRINTLN("[LED] NeoPixel initialized");
}

void LedManager::setBrightness(uint8_t brightness)
{
    m_brightness = brightness;
    if (m_pixels)
    {
        m_pixels->setBrightness(m_brightness);
    }
}

void LedManager::setColor(uint8_t r, uint8_t g, uint8_t b)
{
    setLedRGB(r, g, b);
}

void LedManager::setLedRGB(uint8_t r, uint8_t g, uint8_t b)
{
    if (!m_pixels)
        return;

    // Use uint16_t to prevent overflow during multiplication
    uint16_t br = ((uint16_t)r * m_brightness) / 255;
    uint16_t bg = ((uint16_t)g * m_brightness) / 255;
    uint16_t bb = ((uint16_t)b * m_brightness) / 255;

    m_current_r = (uint8_t)br;
    m_current_g = (uint8_t)bg;
    m_current_b = (uint8_t)bb;

    m_pixels->setPixelColor(0, m_pixels->Color(m_current_r, m_current_g, m_current_b));
    m_pixels->show();
}

void LedManager::breatheEffect(uint8_t baseR, uint8_t baseG, uint8_t baseB, uint32_t now)
{
    const uint32_t breathPeriod = 2000;
    uint32_t elapsed = now - m_pattern_timer;
    uint32_t breathPos = (elapsed % breathPeriod) * 255 / breathPeriod;
    uint8_t breath_value = (breathPos > 127) ? 255 - (breathPos - 128) * 2 : breathPos * 2;

    setLedRGB((baseR * breath_value) / 255,
              (baseG * breath_value) / 255,
              (baseB * breath_value) / 255);
}

void LedManager::blinkEffect(uint8_t onR, uint8_t onG, uint8_t onB,
                             uint8_t offR, uint8_t offG, uint8_t offB,
                             uint32_t interval, uint32_t now)
{
    uint32_t elapsed = now - m_pattern_timer;
    bool state = (elapsed / interval) % 2 == 0;

    if (state)
    {
        setLedRGB(onR, onG, onB);
    }
    else
    {
        setLedRGB(offR, offG, offB);
    }
}

void LedManager::flashEffect(uint8_t flashR, uint8_t flashG, uint8_t flashB,
                             uint8_t baseR, uint8_t baseG, uint8_t baseB, uint32_t now)
{
    const uint32_t flashInterval = 100; // Faster flash
    const uint32_t flashCount = 8;      // More flashes
    const uint32_t totalTime = flashInterval * flashCount * 2;
    uint32_t elapsed = now - m_pattern_timer;

    if (elapsed < totalTime)
    {
        uint32_t flashElapsed = elapsed % (flashInterval * 2);
        if (flashElapsed < flashInterval)
        {
            setLedRGB(flashR, flashG, flashB);
        }
        else
        {
            setLedRGB(baseR, baseG, baseB);
        }
    }
    else
    {
        setLedRGB(baseR, baseG, baseB);
    }
}

void LedManager::fadeEffect(uint32_t now, bool fadeIn)
{
    const uint32_t fadePeriod = 1500;
    uint32_t elapsed = now - m_pattern_timer;
    uint32_t fadePos = (elapsed % fadePeriod) * 255 / fadePeriod;

    uint8_t brightness;
    if (fadeIn)
    {
        brightness = (fadePos > 127) ? 255 - (fadePos - 128) * 2 : fadePos * 2;
    }
    else
    {
        brightness = (fadePos > 127) ? (fadePos - 128) * 2 : 255 - (fadePos * 2);
    }

    setLedRGB(0, brightness, 0); // GREEN only
}

void LedManager::rainbowEffect(uint32_t now)
{
    // Simplified to GREEN fade in/out for startup
    const uint32_t bootTime = 3000;
    uint32_t elapsed = now - m_pattern_timer;

    if (elapsed >= bootTime)
    {
        return; // Signal completion
    }

    uint32_t fadePos = (elapsed * 255) / bootTime;
    uint8_t brightness = (fadePos > 127) ? 255 - (fadePos - 128) * 2 : fadePos * 2;

    setLedRGB(0, brightness, 0); // GREEN only
}

bool LedManager::isPatternComplete(LedPattern pattern, uint32_t now)
{
    uint32_t elapsed = now - m_pattern_timer;

    switch (pattern)
    {
    case LED_STARTUP:
        return elapsed >= 3000;
    case LED_SHARE_FOUND:
        return elapsed >= 800;
    case LED_SHARE_ACCEPTED:
        return elapsed >= 500;
    case LED_SHARE_REJECTED:
        return elapsed >= 800;
    default:
        return false;
    }
}

void LedManager::setPattern(LedPattern pattern)
{
    if (m_current_pattern == pattern)
        return;

    m_current_pattern = pattern;
    m_pattern_timer = millis();
    m_last_update = 0;
    m_blink_state = false;
    m_flash_count = 0;
}

void LedManager::update()
{
    uint32_t now = millis();
    if (now - m_last_update < LED_UPDATE_INTERVAL_MS)
        return;
    m_last_update = now;
    updatePattern(m_current_pattern, now);
}

void LedManager::updatePattern(LedPattern pattern, uint32_t now)
{
    switch (pattern)
    {
    case LED_OFF:
        setLedRGB(0, 0, 0);
        break;
    case LED_STARTUP:
        rainbowEffect(now);
        if (isPatternComplete(LED_STARTUP, now))
            setPattern(LED_CONNECTING);
        break;
    case LED_CONNECTING:
        breatheEffect(0, 0, 200, now); // BLUE only
        break;
    case LED_REGISTERED:
        setLedRGB(0, 0, 150); // BLUE only
        break;
    case LED_MINING_IDLE:
        breatheEffect(0, 0, 150, now); // BLUE only (faster)
        break;
    case LED_MINING_ACTIVE:
        setLedRGB(0, 200, 0); // GREEN only
        break;
    case LED_SHARE_FOUND:
        flashEffect(0, 255, 0, 0, 50, 0, now); // GREEN flash on BLACK
        if (isPatternComplete(LED_SHARE_FOUND, now))
            setPattern(LED_MINING_ACTIVE);
        break;
    case LED_SHARE_ACCEPTED:
        breatheEffect(0, 255, 0, now); // GREEN only
        if (isPatternComplete(LED_SHARE_ACCEPTED, now))
            setPattern(LED_MINING_ACTIVE);
        break;
    case LED_SHARE_REJECTED:
        flashEffect(255, 0, 0, 50, 0, 0, now); // RED flash on dim RED
        if (isPatternComplete(LED_SHARE_REJECTED, now))
            setPattern(LED_MINING_ACTIVE);
        break;
    case LED_ERROR:
        blinkEffect(255, 0, 0, 0, 0, 0, 100, now); // RED fast blink
        break;
    case LED_NO_JOB:
        blinkEffect(0, 0, 200, 0, 0, 50, 500, now); // BLUE slow blink
        break;
    case LED_NO_HEARTBEAT:
        blinkEffect(255, 0, 0, 0, 0, 0, 500, now); // RED slow blink
        break;
    default:
        setLedRGB(0, 0, 0);
        break;
    }
}
