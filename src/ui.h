#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>

// ── 画面レイアウト定数 ────────────────────────────────────────
// 画面解像度: 240×135 (rotation=1)
static constexpr int32_t  UI_HEADER_H  = 20;    // ヘッダー高さ
static constexpr int32_t  UI_FOOTER_H  = 25;    // フッター高さ
static constexpr uint16_t UI_FOOTER_BG = 0x536E; // 濃いティールブルー (#506C70)

// ヘッダー・フッターを描画する（showStatus から毎回呼ばれる）
void drawChrome();

// 画面をリフレッシュして状態ラベルとサブ行を表示する
void showStatus(const char* label, uint16_t color, const char* status = "");

// 波形描画（100サンプルに1点程度に間引く）
void drawWaveform(const int16_t* buf, size_t len);
