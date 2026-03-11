#include "ui.h"

// ============================================================
// drawChrome  ―  ヘッダー・フッターを描画する（毎回 showStatus から呼ぶ）
// ============================================================
void drawChrome()
{
	const int32_t W = M5Cardputer.Display.width();   // 240
	const int32_t H = M5Cardputer.Display.height();  // 135

	// ── ヘッダー ──────────────────────────────────────────────
	// 背景は BLACK（全画面クリア済み）、左端に "CP Recorder"
	M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
	M5Cardputer.Display.setTextDatum(top_left);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.drawString("CP Recorder", 5, 3);
	// ヘッダー下境界線
	M5Cardputer.Display.drawFastHLine(0, UI_HEADER_H - 1, W, (uint16_t)0x4208); // ダークグレー

	// ── フッター ──────────────────────────────────────────────
	M5Cardputer.Display.fillRect(0, H - UI_FOOTER_H, W, UI_FOOTER_H, UI_FOOTER_BG);
}

// ============================================================
// showStatus  ―  画面をリフレッシュして状態を表示
//   sub  : ラベル直下 2px に表示するコンテンツエリア内サブ行
//   hint : フッターに表示する操作ガイド
// ============================================================
void showStatus(const char* label, uint16_t color,
                const char* sub, const char* hint)
{
	const int32_t W        = M5Cardputer.Display.width();    // 240
	const int32_t H        = M5Cardputer.Display.height();   // 135
	const int32_t contentY = UI_HEADER_H;                    // 20
	const int32_t contentH = H - UI_HEADER_H - UI_FOOTER_H; // 90

	// 全画面クリア → クローム再描画
	M5Cardputer.Display.fillRect(0, 0, W, H, BLACK);
	drawChrome();

	// ── フッターへ操作ガイド描画 ──────────────────────────────
	if (hint && hint[0]) {
		M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
		M5Cardputer.Display.setTextDatum(middle_center);
		M5Cardputer.Display.setTextColor(WHITE);
		M5Cardputer.Display.drawString(hint, W / 2, H - UI_FOOTER_H / 2);
	}

	// ── コンテンツエリア上端揃え ──────────────────────────────────────
	// ラベル行: コンテンツ上端 + 2px
	// サブ行:   ラベル行下端 + 2px
	static constexpr int32_t GAP = 2;
	const bool hasSub = sub && sub[0];

	// フォントをセットしてから実際の行高さを取得
	M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
	const int32_t labelH = M5Cardputer.Display.fontHeight();
	M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
	const int32_t subH   = M5Cardputer.Display.fontHeight();

	const int32_t labelY = contentY + GAP;
	const int32_t cx     = W / 2; // 120

	// カラーインジケーター円
	M5Cardputer.Display.fillCircle(cx - 60, labelY + labelH / 2, 7, color);

	// ラベル
	M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
	M5Cardputer.Display.setTextDatum(top_center);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.drawString(label, cx + 15, labelY);

	// サブ行（ラベル下端 + GAP）- 小サイズビットマップフォント（太め）
	if (hasSub) {
		M5Cardputer.Display.setFont(&fonts::Font2);
		M5Cardputer.Display.setTextDatum(top_center);
		M5Cardputer.Display.drawString(sub, cx, labelY + labelH + GAP);
	}
	(void)subH;
}

// ============================================================
// drawTimeIndicator  ―  コンテンツエリア下端に時間を描画
//   total_sec > 0 : "mm:ss / mm:ss" + プログレスバー（PLAY 用）
//   total_sec == 0: "mm:ss" のみ（REC 用）
// ============================================================
void drawTimeIndicator(uint32_t cur_sec, uint32_t total_sec)
{
	const int32_t W            = M5Cardputer.Display.width();  // 240
	const int32_t H            = M5Cardputer.Display.height(); // 135
	const int32_t contentBottom = H - UI_FOOTER_H;             // 110

	M5Cardputer.Display.setFont(&fonts::FreeSansOblique9pt7b);  // イタリック
	const int32_t fh = M5Cardputer.Display.fontHeight();
	const int32_t y  = contentBottom - 2 - fh; // 下端から 2px

	// 前フレームを消去
	M5Cardputer.Display.fillRect(0, y, W, fh, BLACK);

	char buf[20];
	if (total_sec > 0) {
		snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu",
		         cur_sec / 60, cur_sec % 60,
		         total_sec / 60, total_sec % 60);
	} else {
		snprintf(buf, sizeof(buf), "%02lu:%02lu",
		         cur_sec / 60, cur_sec % 60);
	}

	M5Cardputer.Display.setTextDatum(top_center);
	M5Cardputer.Display.setTextColor(GREEN);
	M5Cardputer.Display.drawString(buf, W / 2, y);

	// ── 再生量棒グラフ（PLAY 時のみ）──────────────────────────────
	if (total_sec > 0) {
		const int32_t  barY       = contentBottom - 1;
		const uint16_t DARK_GREEN = 0x0200;
		M5Cardputer.Display.drawFastHLine(0, barY, W, DARK_GREEN);
		const int32_t fillW = (int32_t)((uint64_t)cur_sec * W / total_sec);
		if (fillW > 0)
			M5Cardputer.Display.drawFastHLine(0, barY, fillW, GREEN);
	}
}
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
