# m5cp-audio — 開発骨子ドキュメント

将来のセッションで即座に開発を再開できるよう、アーキテクチャ・ピン配置・コード構造・未実装項目を記録する。

---

## 1. ハードウェア

| 項目 | 詳細 |
|---|---|
| ボード | M5Stack StampS3 (ESP32-S3) + M5Cardputer キーボード/ディスプレイ |
| PlatformIO board | `m5stack-stamps3` |
| 表示 | M5GFX/LovyanGFX, 240×135px, rotation=1 |
| 音声入力 | PCM1808 外部 ADC (I2S_NUM_1) |
| 音声出力 | M5Cardputer 内蔵スピーカー (I2S_NUM_0) |
| ストレージ | microSD (SPI) |

### I2S ピン配置

| 役割 | ピン |
|---|---|
| BCK (ADC) | GPIO 3 |
| LRCK (ADC) | GPIO 4 |
| DIN (ADC) | GPIO 5 |
| MCK (ADC) | GPIO 13 |

### SD SPI ピン配置

| 役割 | ピン |
|---|---|
| SCK | GPIO 40 |
| MISO | GPIO 39 |
| MOSI | GPIO 14 |
| CS | GPIO 12 |

---

## 2. ソフトウェア構成

### platformio.ini の lib_deps

```ini
m5stack/M5Unified
m5stack/M5GFX
m5stack/M5Cardputer @ ^1.1.1
m5stack/M5Unified @ ^0.2.2
https://github.com/pschatzmann/arduino-audio-tools.git
```

### ソースファイル一覧

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | メインループ・録音/再生ステートマシン |
| `src/ui.h` | UI 定数・関数宣言 |
| `src/ui.cpp` | 全表示関数の実装 |
| `src/pcm1808.h` | PCM1808 I2S ADC ドライバ (ヘッダ) |
| `src/pcm1808.cpp` | PCM1808 I2S ADC ドライバ (実装) |

---

## 3. アーキテクチャ

### デュアルコア録音

```
Core 1 (loop):   i2s_read() → ping_pong[buf_idx] → xQueueSend(rec_queue, buf_idx)
                 → M5.update() → トリガー判定 → 停止チェック

Core 0 (sd_task): xQueueReceive(rec_queue) → file.write() → drawWaveform()
                  受信 0xFF (QUEUE_STOP_SIGNAL) → finalizeRecFile() → play_requested=true
```

- ピンポンバッファ: `int16_t ping_pong[2][512]`
- Queue 要素: `uint8_t`（0 or 1 = バッファ index、0xFF = 停止シグナル）

### 録音 WAV フォーマット

- サンプルレート: 16,000 Hz
- ビット深度: 16-bit (PCM)
- チャンネル: モノラル
- ファイル名: `/rec0000.wav` ～ `/rec0999.wav`（同名ならインクリメント）
- 最大録音長: 約 1時間 (MAX_RECORDING_SIZE = 16000×2×3600 bytes ≒ 115MB)
- WAV ヘッダ: 録音開始時にダミー書き込み → `finalizeRecFile()` で実サイズに上書き

### ステートマシン (loop())

```
[Ready] --トリガー--> [REC中] --トリガー--> (stop_requested=true)
                                           --> sd_task が finalize --> [PLAY中]
[PLAY中] --再生完了--> [Ready]
[PLAY中] --トリガー--> 強制停止 --> [Ready]
```

### トリガー条件

```cpp
const bool trigger = M5Cardputer.BtnA.wasClicked() ||
                     (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed());
```

任意のキー押下 = BtnA クリックと同等。

---

## 4. 画面 UI レイアウト (240×135)

```
┌─────────────────────────────────┐  y=0
│  CP Recorder          (Header)  │  H=20  FreeSans9pt7b, 左揃え
├─────────────────────────────────┤  y=20  ダークグレー境界線 (0x4208)
│                                 │
│  [● label]  (FreeSansBoldOblique12pt7b)   コンテンツ上端+2px
│  sub line   (Font2 small bitmap)           ラベル下端+2px
│                                 │
│  mm:ss / mm:ss   (FreeSansOblique9pt7b)   コンテンツ下端から2px+fh
│─────────────────────────────────│  y=109 プログレスバー(1px)
├─────────────────────────────────┤  y=110 (H - UI_FOOTER_H)
│  hint text  (FreeSans9pt7b)     │  H=25  UI_FOOTER_BG=0x536E (ダークティール)
└─────────────────────────────────┘  y=135
```

### UI 定数 (ui.h)

```cpp
static constexpr int32_t  UI_HEADER_H  = 20;
static constexpr int32_t  UI_FOOTER_H  = 25;
static constexpr uint16_t UI_FOOTER_BG = 0x536E;  // ダークティール
```

### 関数シグネチャ (ui.h)

```cpp
void drawChrome();
void showStatus(const char* label, uint16_t color,
                const char* sub = "", const char* hint = "");
void drawWaveform(const int16_t* buf, size_t len);
void drawTimeIndicator(uint32_t cur_sec, uint32_t total_sec);
```

### 各フォントの用途

| フォント | 用途 |
|---|---|
| `FreeSansBoldOblique12pt7b` | ステータスラベル (REC / PLAY / Ready) |
| `FreeSans9pt7b` | ヘッダータイトル・フッターヒント |
| `FreeSansOblique9pt7b` | タイムインジケーター (イタリック) |
| `Font2` (ビットマップ 8px) | サブ行 (ファイル名など) |

### showStatus の呼び出し例

```cpp
showStatus("Ready", WHITE, "",       "Press any key to record");
showStatus("REC",   RED,   "",       "Press any key to stop");
showStatus("PLAY",  BLUE,  filename, "Press any key to stop");
```

---

## 5. タイムインジケーター (drawTimeIndicator)

- **REC モード** (`total_sec == 0`): `"mm:ss"` のみ表示、バーなし
- **PLAY モード** (`total_sec > 0`): `"mm:ss / mm:ss"` + y=109 に 1px プログレスバー
  - 背景色: `0x0200` (ダークグリーン)、進捗色: `GREEN`
- `loop()` 内で `millis()` ベースの 1Hz レートリミッターで更新

---

## 6. 再生実装 (startPlayback / isPlaybackDone)

```cpp
// 開始: lineIn.end() → SD.open() → seek(sizeof(WAVHeader)) → Speaker.begin() → playRaw()
void startPlayback(const char* fname);

// 継続/完了判定: !Speaker.isPlaying() かつ available() → 次チャンク供給
//               ファイル末尾 → Speaker.end() → return true
bool isPlaybackDone();
```

- 再生バッファ: `int16_t play_buf[CHUNK_SAMPLES * 4]` (2048 サンプル)
- `M5Cardputer.Speaker.setVolume(200)`
- 再生終了後の次回録音時に `lineIn.begin(3, 4, 5, 13)` で ADC を再初期化

---

## 7. 未実装 / TODO

| 項目 | 場所 | 概要 |
|---|---|---|
| `drawWaveform()` | `src/ui.cpp` | 現在スタブのみ。len/100点に間引き、`writeFastVLine` で描画推奨 |
| SD 初期化エラー表示 | `setup()` | SD 失敗時に画面にエラー表示 |
| ファイル上限エラー表示 | `openRecFile()` | `file_counter >= 1000` 時の画面表示 |
| 録音開始失敗の表示 | `loop()` | `openRecFile()` 失敗時の画面表示 |
| ファイル選択 UI | 未実装 | 録音済みファイルをリスト表示・選択再生 |
| `file_counter` の永続化 | 未実装 | 起動時に SD から既存ファイル番号を復元 |

---

## 8. ブランチ・コミット履歴 (主要)

| コミット | 内容 |
|---|---|
| `aba58ad` | 最初の再生機能実装 |
| `0f6cbd4` | キーボード入力サポート |
| `2ceb287` | 画面 UI 設計化 |
| `148a833` | UI を ui.h/ui.cpp に分離 |
| `7dec374` | 再生中キー押下での暴走バグ修正 |
| `31fb5d0` | タイムインジケーター・プログレスバー・UI ポリッシュ |

現在の作業ブランチ: `doubleb`

---

## 9. ビルド・書き込み

```bash
# ビルド
pio run

# 書き込み + モニター
pio run --target upload && pio device monitor
```
