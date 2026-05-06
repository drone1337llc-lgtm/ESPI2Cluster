// Author : Sergio WIlliams
// GPL - 3.0 - or -later

#ifndef I2C_MUX_H
#define I2C_MUX_H

#include <Wire.h>
#include <TCA9548A.h>
#include "config.h"

#define TCA9548A_RST_PIN 7

class I2CMux
{
public:
    I2CMux(uint8_t sda_pin = I2C_MASTER_SDA, uint8_t scl_pin = I2C_MASTER_SCL);

    bool begin();
    bool selectChannel(uint8_t channel);
    void deselectChannel(uint8_t channel);
    void deselectAll();
    uint8_t getSelectedChannels() const { return _selected_channels; }
    bool isConnected(uint8_t channel);
    bool isMuxPresent() const { return _mux_present; }
    void hardReset();

private:
    uint8_t _sda_pin;
    uint8_t _scl_pin;
    uint8_t _selected_channels;
    bool _mux_present;
    TCA9548A _mux;
};

extern I2CMux g_i2c_mux;

#endif
