#pragma once

#include <Arduino.h>

// ── アプリ設定構造体 ──────────────────────────────────────────
struct AppConfig {
    char    current_file[32];  // カレント WAV ファイル名（例: /rec0000.wav）
    bool    use_rmt;           // RMT 端子を使用するか（有効時は冒頭無音を自動スキップ）
    uint8_t speaker_volume;    // スピーカーボリューム (0-255)
};

// グローバル設定インスタンス
extern AppConfig g_config;

// SD カードから設定を読み込む（ファイルがない場合はデフォルト値で新規作成）
void loadConfig();

// SD カードに設定を書き込む
void saveConfig();
