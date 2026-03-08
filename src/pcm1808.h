#pragma once

#include <Arduino.h>
#include "driver/i2s.h"

class PCM1808 {
public:
    bool begin(int bck_pin, int lrck_pin, int din_pin, int mck_pin,
               uint32_t sample_rate = 16000);
    void end();
    bool isEnabled() const { return _enabled; }
    size_t read(int16_t* buf, size_t samples);

private:
    bool _enabled = false;
};