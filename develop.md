# m5cp-audio — 開発骨子

ブランチ: `doubleb` / ビルド: `pio run` / 書き込み: `pio run --target upload`

---

## ハードウェア

| | |
|---|---|
| ボード | M5Stack StampS3 (ESP32-S3) + M5Cardputer |
| 表示 | M5GFX 240×135px rotation=1 |
| ADC | PCM1808 I2S_NUM_1 (BCK=3, LRCK=4, DIN=5, MCK=13) |
| Speaker | I2S_NUM_0 (内蔵) |
| SD | SPI (SCK=40, MISO=39, MOSI=14, CS=12) |

**⚠️ I2S 制約**: I2S0/I2S1 が PLL クロックを共有。`Speaker.begin()` と `lineIn.begin()` の同時稼働不可 → REC 中パススルー再生は実現不可。

---

## ソース構成

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | ステートマシン・録音/再生・VU 計算・RMT デバウンス |
| `src/ui.h / ui.cpp` | 全表示関数（ステータス/ブラウザ/設定/VUメーター） |
| `src/config.h / .cpp` | INI ファイル読み書き (`AppConfig`) |
| `src/pcm1808.h / .cpp` | I2S ADC ドライバ |

---

## アーキテクチャ

```
Core1 (loop):    lineIn.read() → ping_pong[buf_idx] → xQueueSend → VUMeter更新
Core0 (sd_task): xQueueReceive → file.write() → (0xFF受信) → finalize → play_requested=true
```

ステートマシン:
```
Ready ←→ Browse
      ←→ Settings
      →  RMT Wait → (G6=L) → REC → (finalize) → PLAY → Ready
      →  REC → (finalize) → PLAY → Ready
```
トリガー: `BtnA.wasClicked()` または任意キー押下

---

## 画面レイアウト (240×135)

```
y=0   Header H=20  "CP Recorder"  [rmt バッジ: RMT有効時のみ右端に赤地黄文字]
y=20  Content H=90
        ● label (FreeSansBoldOblique12pt7b)  +2px
        sub     (Font2)                       +2px
        [VU meter 24seg FL管風]               動的算出・上下4px pad
        mm:ss / mm:ss (FreeSansOblique9pt7b)  下端-2px-fh
y=109  progress bar 1px
y=110  Footer H=25  hint (FreeSans9pt7b)  bg=0x536E
```

UI 定数: `UI_HEADER_H=20` / `UI_FOOTER_H=25` / `UI_VU_XL=4`

---

## VU メーター仕様

- 24 セグメント + 2px ギャップ、-40〜0dBFS
- EMA α=0.1 (τ≈300ms)、ピークホールド 600ms→2dB/frame 降下
- -18dBFS = 0VU 基準: バー上端にシアン 2px マーカー
- ゾーン: 暗緑`0x0060`/明緑`0x05C0` | 暗緑`0x0100`/`GREEN` | 暗黄`0x2100`/`YELLOW`
- REC: `lineIn.read()` 直後・`xQueueSend()` 前に更新（~31fps）
- PLAY: 1024サンプルチャンクを 512×2 分割で呼び、EMA 刻みを REC と統一 (~16fps)

---

## 録音フォーマット

16kHz / 16bit mono / `/rec0000.wav`〜`/rec0999.wav` / 最大1時間 / WAVヘッダは終了時上書き

---

## 設定ファイル (`/<APP_NAME>_config.ini`)

| キー | 型 | デフォルト | 説明 |
|---|---|---|---|
| `current_file` | string | (空) | 最後に録音/選択した WAV ファイルパス |
| `use_rmt` | bool | No | RMT スイッチ使用（有効時は再生冒頭の無音を自動スキップ） |

設定画面: Ready で `S` キー → `;`/`.` で移動、`,`/`/` で Yes/No 切り替え、Enter/他キーで保存、`` ` `` でキャンセル。`current_file` は参照専用。

---

## RMT スイッチ仕様

- GPIO6 (`INPUT_PULLUP`)、通常 H / ON で L
- デバウンス 80 ms（ファイルスコープ変数で実装、ループ先頭で 1 回読み）
- `use_rmt=Yes` + `R` キー押下時 G6=H なら `rmt_waiting` 状態へ遷移し `RMT Wait` 画面表示
- G6=L 検出で録音開始、G6=H 復帰で録音停止
- RMT Wait 中も任意キーでキャンセル可
- ヘッダー右端に `rmt` バッジ（赤地・黄文字・Font2）を常時表示

---

## TODO

| 項目 | 場所 |
|---|---|
| `drawWaveform()` 実装 | `ui.cpp` (現在スタブ) |
| SD/録音開始失敗のエラー表示 | `setup()` / `loop()` |
| `file_counter` 永続化 | 未実装 |

---

## 最近のコミット

| コミット | 内容 |
|---|---|
| `36d9d58` | ANSI VU メーター追加 |
| `b631f8c` | 24 セグメント FL 管風リデザイン |
| `e653c28` | REC 画面にも VU 表示 |
| `68fc1fb` | PLAY ~16fps 調整・パススルー限界を develop.md に記録 |
| —        | RMT スイッチ対応（GPIO6 デバウンス 80ms） |
| —        | 設定画面 (S キー)・INI 読み書き |
| —        | ヘッダー RMT バッジ表示 |

