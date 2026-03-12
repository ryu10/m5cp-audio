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
//   sub  : ラベル直下 2px に表示するサブ行（コンテンツエリア内）
//   hint : フッターに表示する操作ガイド
void showStatus(const char* label, uint16_t color,
                const char* sub = "", const char* hint = "");

// 波形描画（100サンプルに1点程度に間引く）
void drawWaveform(const int16_t* buf, size_t len);

// 再生中タイムインジケータをコンテンツエリア下端に描画する
//   cur_sec   : 現在の再生位置（秒）
//   total_sec : 総再生時間（秒）
void drawTimeIndicator(uint32_t cur_sec, uint32_t total_sec);

// ファイルブラウザ: SDスキャン・初期選択位置計算
void initFileBrowser(const char* current_file);
// カレントディレクトリの .wav 一覧をスクロール・ハイライト付きで表示
void showFileBrowser();
// 選択を一つ上 / 下に移動
void browseMoveUp();
void browseMoveDown();
// 現在選択中のファイル名（"/" なし）を返す。0件なら nullptr
const char* browseSelectedFile();

// ── VU メーター定数 ──────────────────────────────────────────
// UI_VU_Y / UI_VU_H は drawVUMeter() 内でフォント高さから動的に算出する
// ここでは左右マージンのみ定義
static constexpr int32_t UI_VU_XL = 4;    // 左右マージン

// 水平 VU メーターを描画する（PLAY 画面用）
//   rms  : 0.0〜1.0（computeRMS() の戻り値）
//   表示範囲: -40dBFS〜0dBFS
//   バリスティクス: EMA α=0.1（τ≈300ms @32ms 更新周期、ANSI VU 相当）
//   基準: -18dBFS = 0 VU（白ティック表示）
//   色:   暗緑(-40〜-20dBFS) / 緑(-20〜-3dBFS) / 黄(-3〜0dBFS)
//   ピークホールド: 1.5s 保持 → 1dB/フレームで降下、黄色域以上=赤
void drawVUMeter(float rms);

// VU メーター内部状態（平滑化値・ピーク）をリセット（再生開始時に呼ぶ）
void resetVUMeter();
