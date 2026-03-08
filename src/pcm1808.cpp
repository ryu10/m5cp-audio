#include "pcm1808.h"

static constexpr i2s_port_t I2S_PORT = I2S_NUM_1;

bool PCM1808::begin(int bck_pin, int lrck_pin, int din_pin, int mck_pin,
                    uint32_t sample_rate)
{
    if (_enabled) return true;

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = sample_rate,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,  // I2S_CHANNEL_FMT_RIGHT_LEFT
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 256,
        .use_apll             = false, // default = false
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_384, // 256x, 384x, 512x
    };

    if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
        printf("PCM1808: failed to install I2S driver\n");
        return false;
    }

    i2s_pin_config_t pins = {
        .mck_io_num   = mck_pin,
        .bck_io_num   = bck_pin,
        .ws_io_num    = lrck_pin,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = din_pin,
    };

    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        printf("PCM1808: failed to set I2S pins\n");
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    _enabled = true;
    return true;
}

void PCM1808::end()
{
    if (!_enabled) return;
    i2s_driver_uninstall(I2S_PORT);
    _enabled = false;
}

size_t PCM1808::read(int16_t* buf, size_t samples)
{
    if (!_enabled || !buf) return 0;

    const size_t raw_bytes = samples * sizeof(int32_t);
    int32_t* raw = (int32_t*)malloc(raw_bytes);
    if (!raw) return 0;

    size_t bytes_read = 0;
    i2s_read(I2S_PORT, raw, raw_bytes, &bytes_read, portMAX_DELAY);
    // デバッグ: 最初の数サンプルを出力
    // for (int d = 0; d < 4; d++) {
    //     printf("raw[%d] = 0x%08lX (%ld)\n", d, (unsigned long)raw[d], (long)raw[d]);
    // }   

    size_t samples_read = bytes_read / sizeof(int32_t);

    for (size_t i = 0; i < samples_read; i++) {
        buf[i] = (int16_t)(raw[i] >> 16); // 24 ビットデータが 32 ビット整数の上位に配置されているため、16 ビット右シフトしてから int16_t にキャスト
    }

    free(raw);
    return samples_read;
}