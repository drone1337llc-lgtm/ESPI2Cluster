// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#include "i2c_mux.h"

I2CMux g_i2c_mux;

I2CMux::I2CMux(uint8_t sda_pin, uint8_t scl_pin)
    : _sda_pin(sda_pin), _scl_pin(scl_pin), _selected_channels(0), _mux_present(false), _mux(TCA9548A_ADDR)
{
    pinMode(TCA9548A_RST_PIN, OUTPUT);
    digitalWrite(TCA9548A_RST_PIN, HIGH);
}

void I2CMux::hardReset()
{
    DEBUG_PRINTLN("[I2C] Performing TCA9548A hard reset...");
    digitalWrite(TCA9548A_RST_PIN, LOW);
    delay(10);
    digitalWrite(TCA9548A_RST_PIN, HIGH);
    delay(10);
    DEBUG_PRINTLN("[I2C] TCA9548A reset complete");
}

bool I2CMux::begin()
{
    hardReset();

    // Set maximum I2C buffer size
    Wire.setBufferSize(I2C_BUFFER_SIZE);
    Wire.setClock(I2C_SPEED);

    _mux.begin(Wire);

    // Test communication
    Wire.beginTransmission(TCA9548A_ADDR);
    Wire.write(0x00);
    uint8_t result = Wire.endTransmission();

    if (result == 0)
    {
        _mux_present = true;
        _selected_channels = 0;
        DEBUG_PRINTLN("[I2C] TCA9548A mux initialized");

        // Verify channels
        DEBUG_PRINTLN("[I2C] Verifying mux channels...");
        uint8_t devices_found = 0;

        for (uint8_t ch = 0; ch < 8; ch++)
        {
            _mux.openChannel(ch);
            delayMicroseconds(100);

            Wire.beginTransmission(WORKER_I2C_ADDR);
            if (Wire.endTransmission() == 0)
            {
                DEBUG_PRINTF("[I2C] Channel %d: Worker found\n", ch);
                devices_found++;
            }

            _mux.closeChannel(ch);
        }

        if (devices_found == 0)
        {
            DEBUG_PRINTLN("[I2C] WARNING: No workers found");
        }
        else
        {
            DEBUG_PRINTF("[I2C] Found %d worker(s)\n", devices_found);
        }

        return true;
    }
    else
    {
        _mux_present = false;
        DEBUG_PRINTF("[I2C] WARNING: No TCA9548A mux detected (error: %d)\n", result);
        return false;
    }
}

bool I2CMux::selectChannel(uint8_t channel)
{
    if (channel > 7)
        return false;

    if (!_mux_present)
    {
        _selected_channels = 0;
        return true;
    }

    _mux.openChannel(channel);
    _selected_channels |= (1 << channel);
    delayMicroseconds(10);
    return true;
}

void I2CMux::deselectChannel(uint8_t channel)
{
    if (channel > 7)
        return;

    if (!_mux_present)
    {
        _selected_channels = 0;
        return;
    }

    _mux.closeChannel(channel);
    _selected_channels &= ~(1 << channel);
}

void I2CMux::deselectAll()
{
    if (!_mux_present)
    {
        _selected_channels = 0;
        return;
    }

    _mux.closeAll();
    _selected_channels = 0;
}

bool I2CMux::isConnected(uint8_t channel)
{
    if (!selectChannel(channel))
        return false;

    Wire.beginTransmission(WORKER_I2C_ADDR);
    bool connected = (Wire.endTransmission() == 0);
    deselectChannel(channel);
    return connected;
}
