# m5cp-audio

JA

M5Cardputer オーディオキャプチャ。内蔵マイクおよび Cardputer-ADV EXT 端子に接続した PCM1808 に対応します。レトロパソコンのカセットテープインターフェイスに接続して使用するケースを想定しています。

Release 0.1.0 M5buner share: X2S7RMCggJOnHdi8

## 基本機能

- 44.1kHz / モノ / 16bit リニア PCM
- SD カードに WAV 形式で保存
- 入力：PCM1808 ライン入力または内蔵マイク（`#define USE_PCM1808` でコンパイル時切替え）
- 出力：M5Cardputer-adv ライン出力または内蔵スピーカー（HW切り替え式）
- 録音中の VU メーター表示（FL 管風 24 セグメント）
- SD カードファイルブラウザ
- RMT 端子対応（リレースイッチで録音開始/停止）
- 設定画面（S キー）で各パラメータを変更・保存

（以下は To Do）

- ファイル別に再生時出力レベル設定を保存可能

## EXT 端子接続

PCM1808

- BCK=G3
- LRCK=G4
- DIN=G5 
- MCK=G13

RMT 端子
- G6 Tip — リレースイッチ（常開）の片端
- GND Ring — リレースイッチのもう片端
- GPIO6 は INPUT_PULLUP（通常 H、スイッチ ON で L）
- デバウンス: 80 ms（金属バネ式リレー対応）

注：ジャック配線によりプラグ未接続時は Tip-Ring 短絡となっているため、「Rec 開始時最初から L だった場合はすぐに録音開始」を実装

## PCM1808 パラメータ

- LRCK (fs) = 44.1kHz
- MCLK = 384fs = 16.9344MHz (`I2S_MCLK_MULTIPLE_384`)
- I2S 受信フォーマット = 32bit スロット（`I2S_BITS_PER_SAMPLE_32BIT`）
- WAV 保存フォーマット = モノラル 16bit PCM（アプリ側で `int16_t` へ変換）

```cpp
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = sample_rate,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_384, 
    };
```