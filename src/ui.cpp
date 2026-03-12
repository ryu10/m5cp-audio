#include "ui.h"
#include <SD.h>

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
// showFileBrowser  ―  SD カードルートの .wav ファイル一覧を表示
// ============================================================
void showFileBrowser()
{
	const int32_t W        = M5Cardputer.Display.width();    // 240
	const int32_t H        = M5Cardputer.Display.height();   // 135
	const int32_t contentY = UI_HEADER_H;                    // 20
	const int32_t contentH = H - UI_HEADER_H - UI_FOOTER_H; // 90

	M5Cardputer.Display.fillRect(0, 0, W, H, BLACK);
	drawChrome();

	// フッターにヒント
	M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
	M5Cardputer.Display.setTextDatum(middle_center);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.drawString("any key to return", W / 2, H - UI_FOOTER_H / 2);

	// ファイルリスト（Font2 = sub と同じ小フォント）
	M5Cardputer.Display.setFont(&fonts::Font2);
	const int32_t lineH    = M5Cardputer.Display.fontHeight() + 1;
	const int32_t maxLines = contentH / lineH;

	File root = SD.open("/");
	if (!root) {
		M5Cardputer.Display.setTextDatum(middle_center);
		M5Cardputer.Display.setTextColor(RED);
		M5Cardputer.Display.drawString("SD open error", W / 2, contentY + contentH / 2);
		return;
	}

	int lineCount = 0;
	for (;;) {
		File entry = root.openNextFile();
		if (!entry) break;
		if (entry.isDirectory()) { entry.close(); continue; }

		const char* rawName     = entry.name();
		const char* displayName = (rawName[0] == '/') ? rawName + 1 : rawName;

		// 拡張子 .wav（大小文字不問）
		const size_t len = strlen(displayName);
		if (len > 4) {
			const char* ext = displayName + len - 4;
			const bool isWav = (ext[0] == '.') &&
			                   (ext[1] == 'w' || ext[1] == 'W') &&
			                   (ext[2] == 'a' || ext[2] == 'A') &&
			                   (ext[3] == 'v' || ext[3] == 'V');
			if (isWav && lineCount < maxLines) {
				M5Cardputer.Display.setTextDatum(top_left);
				M5Cardputer.Display.setTextColor(WHITE);
				M5Cardputer.Display.drawString(
					displayName, 4, contentY + lineCount * lineH);
				lineCount++;
			}
		}
		entry.close();
	}
	root.close();

	if (lineCount == 0) {
		M5Cardputer.Display.setFont(&fonts::Font2);
		M5Cardputer.Display.setTextDatum(middle_center);
		M5Cardputer.Display.setTextColor(0x7BEF);
		M5Cardputer.Display.drawString("(no wav files)", W / 2, contentY + contentH / 2);
	}
}

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
		const uint16_t DARK_GREEN = 0x0380;
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

// ── VU メーター状態（ファイルスコープ、再生ごとに resetVUMeter() でリセット）──
static float     s_vu_smoothed = 0.0f;
static float     s_vu_peak_db  = -40.0f;
static uint32_t  s_vu_peak_ms  = 0;

void resetVUMeter()
{
	s_vu_smoothed = 0.0f;
	s_vu_peak_db  = -40.0f;
	s_vu_peak_ms  = 0;
}

// ============================================================
// drawVUMeter  ―  PLAY 画面の空白帯に 16 セグメント VU バーを描画する
//   rms  : 0.0〜1.0（computeRMS() の戻り値）
//   バリスティクス: EMA α=0.1 → τ≈300ms（ANSI C16.5 VU 仕様相当）
//   表示範囲: -40dBFS〜0dBFS / 16 セグメント + 2px ギャップ（FL 管風）
//   -18dBFS = 0 VU 基準をシアン 2px マーカーでバー上端に表示
//   色: 暗緑(-40〜-20dBFS) / 緑(-20〜-3dBFS) / 黄(-3〜0dBFS)
//   ピーク: 600ms ホールド後 2dB/フレーム降下、単一セグメント点灯
// ============================================================
void drawVUMeter(float rms)
{
	const int32_t W             = M5Cardputer.Display.width();
	const int32_t H             = M5Cardputer.Display.height();
	const int32_t contentBottom = H - UI_FOOTER_H;

	// ── レイアウト算出（showStatus / drawTimeIndicator と同一ロジック）──
	static constexpr int32_t GAP = 2;
	M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
	const int32_t labelH    = M5Cardputer.Display.fontHeight();
	M5Cardputer.Display.setFont(&fonts::Font2);
	const int32_t subBottom = UI_HEADER_H + GAP + labelH + GAP
	                          + M5Cardputer.Display.fontHeight();
	M5Cardputer.Display.setFont(&fonts::FreeSansOblique9pt7b);
	const int32_t timeTop   = contentBottom - 2 - M5Cardputer.Display.fontHeight();

	static constexpr int32_t VU_PAD = 4;
	const int32_t vuY = subBottom + VU_PAD;
	const int32_t vuH = timeTop - VU_PAD - vuY;
	if (vuH <= 2) return;

	const int32_t barW = W - UI_VU_XL * 2;

	// ── バリスティクス: EMA（α=0.1 → τ≈300ms）────────────────────
	static constexpr float ALPHA = 0.1f;
	s_vu_smoothed = ALPHA * rms + (1.0f - ALPHA) * s_vu_smoothed;

	// ── dBFS 変換 ─────────────────────────────────────────────────
	static constexpr float DB_FLOOR  = -40.0f;
	static constexpr float DB_RANGE  =  40.0f;
	static constexpr float DB_GREEN1 = -20.0f;
	static constexpr float DB_GREEN2 =  -3.0f;
	static constexpr float DB_VU0    = -18.0f;

	const float db = (s_vu_smoothed > 1e-6f)
	                 ? fmaxf(20.0f * log10f(s_vu_smoothed), DB_FLOOR)
	                 : DB_FLOOR;

	// ── ピークホールド更新（600ms ホールド後 2dB/フレーム降下）──────
	static constexpr uint32_t PEAK_HOLD_MS = 600;
	static constexpr float    PEAK_FALL    = 2.0f;
	const uint32_t now = millis();
	if (db >= s_vu_peak_db) {
		s_vu_peak_db = db;
		s_vu_peak_ms = now;
	} else if (now - s_vu_peak_ms > PEAK_HOLD_MS) {
		s_vu_peak_db -= PEAK_FALL;
		if (s_vu_peak_db < DB_FLOOR) s_vu_peak_db = DB_FLOOR;
	}

	// ── セグメントレイアウト ──────────────────────────────────────
	static constexpr int32_t N_SEG   = 24;
	static constexpr int32_t SEG_GAP = 2;
	const int32_t segW      = (barW - SEG_GAP * (N_SEG - 1)) / N_SEG;
	const int32_t totalUsed = segW * N_SEG + SEG_GAP * (N_SEG - 1);
	const int32_t xOff      = UI_VU_XL + (barW - totalUsed) / 2;

	// ── dB → セグメントインデックス（0..N_SEG）────────────────────
	auto dbToSeg = [&](float d) -> int32_t {
		float f = (d - DB_FLOOR) / DB_RANGE;
		if (f < 0.0f) f = 0.0f;
		if (f > 1.0f) f = 1.0f;
		return (int32_t)(f * (float)N_SEG);
	};

	const int32_t fillSeg = dbToSeg(db);
	int32_t       peakSeg = dbToSeg(s_vu_peak_db);
	if (peakSeg >= N_SEG) peakSeg = N_SEG - 1;
	const bool peakValid = s_vu_peak_db > DB_FLOOR + 1.0f;

	// ── 0 VU 基準マーカー（バー上端 1px 上にシアン 2px）─────────
	{
		const float   refF = (DB_VU0 - DB_FLOOR) / DB_RANGE;
		const int32_t refX = xOff + (int32_t)(refF * (float)totalUsed);
		M5Cardputer.Display.fillRect(refX, vuY - 1, 2, 1, (uint16_t)0x07FF);
	}

	// ── セグメント描画（FL 管風）──────────────────────────────────
	for (int32_t i = 0; i < N_SEG; i++) {
		const int32_t sx    = xOff + i * (segW + SEG_GAP);
		const float   segDb = DB_FLOOR + (i + 0.5f) * DB_RANGE / (float)N_SEG;

		// ゾーン色決定（点灯色 / 消灯暗色 / ピーク色）
		uint16_t litColor, dimColor, pkColor;
		if (segDb >= DB_GREEN2) {
			litColor = YELLOW;           dimColor = (uint16_t)0x2100; pkColor = YELLOW;
		} else if (segDb >= DB_GREEN1) {
			litColor = GREEN;            dimColor = (uint16_t)0x0100; pkColor = GREEN;
		} else {
			litColor = (uint16_t)0x05C0; dimColor = (uint16_t)0x0060; pkColor = GREEN;
		}

		uint16_t color;
		if (i < fillSeg) {
			color = litColor;
		} else if (peakValid && i == peakSeg) {
			color = pkColor;
		} else {
			color = dimColor;
		}
		M5Cardputer.Display.fillRect(sx, vuY, segW, vuH, color);
	}
}
