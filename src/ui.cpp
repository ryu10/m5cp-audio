#include "ui.h"

// ============================================================
// drawChrome  ―  ヘッダー・フッターを描画する（毎回 showStatus から呼ぶ）
// ============================================================
void drawChrome()
{
	const int32_t W = M5Cardputer.Display.width();   // 240
	const int32_t H = M5Cardputer.Display.height();  // 135

	// ── ヘッダー ──────────────────────────────────────────────
	// 背景は BLACK（全画面クリア済み）、左端に "RECORDER"
	M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
	M5Cardputer.Display.setTextDatum(top_left);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.drawString("RECORDER", 5, 3);
	// ヘッダー下境界線
	M5Cardputer.Display.drawFastHLine(0, UI_HEADER_H - 1, W, (uint16_t)0x4208); // ダークグレー

	// ── フッター ──────────────────────────────────────────────
	M5Cardputer.Display.fillRect(0, H - UI_FOOTER_H, W, UI_FOOTER_H, UI_FOOTER_BG);
}

// ============================================================
// showStatus  ―  画面をリフレッシュして状態を表示
//   コンテンツエリア (y=20..109, 90px) にラベルを縦中央揃えで描画
//   ヒントテキスト（status）はフッター内に描画
// ============================================================
void showStatus(const char* label, uint16_t color, const char* status)
{
	const int32_t W        = M5Cardputer.Display.width();    // 240
	const int32_t H        = M5Cardputer.Display.height();   // 135
	const int32_t contentY = UI_HEADER_H;                    // 20
	const int32_t contentH = H - UI_HEADER_H - UI_FOOTER_H; // 90

	// 全画面クリア → クローム再描画
	M5Cardputer.Display.fillRect(0, 0, W, H, BLACK);
	drawChrome();

	// ── フッターへヒントテキスト描画 ──────────────────────────────
	if (status && status[0]) {
		M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
		M5Cardputer.Display.setTextDatum(middle_center);
		M5Cardputer.Display.setTextColor(WHITE);
		M5Cardputer.Display.drawString(status, W / 2, H - UI_FOOTER_H / 2);
	}

	// ── コンテンツエリア縦中央（ラベル行のみ）─────────────────────
	static constexpr int32_t LABEL_H = 18; // FreeSansBoldOblique12pt7b ≈ 18px
	const int32_t labelY = contentY + (contentH - LABEL_H) / 2; // ≈ 56
	const int32_t cx     = W / 2;                                // 120

	// カラーインジケーター円（ラベル行の左側）
	M5Cardputer.Display.fillCircle(cx - 60, labelY + LABEL_H / 2, 7, color);

	// ラベル（top_center）
	M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
	M5Cardputer.Display.setTextDatum(top_center);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.drawString(label, cx + 15, labelY);
}

// ============================================================
// drawWaveform  ―  波形描画（100サンプルに1点程度に間引く）
// ============================================================
void drawWaveform(const int16_t* buf, size_t len)
{
	// ここに波形描画を実装 ────────────────────────────────────────
	// 推奨: len/100 点程度に間引いて writeFastVLine で描画
	// 高さ: (sample >> 6) を Display.height()/2 に加算
	// 前フレームの線を TFT_BLACK で消してから描く
	// ────────────────────────────────────────────────────────────
	(void)buf;
	(void)len;
}
