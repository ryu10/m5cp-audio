#pragma once

#include <Arduino.h>

// ── アプリ設定構造体 ──────────────────────────────────────────
struct AppConfig {
    char current_file[32];  // カレント WAV ファイル名（例: /rec0000.wav）
    bool skip_silence;      // 冒頭無音部分をスキップするか
    bool use_rmt;           // RMT 端子を使用するか
};

// グローバル設定インスタンス
extern AppConfig g_config;

// SD カードから設定を読み込む（ファイルがない場合はデフォルト値で新規作成）
void loadConfig();

// SD カードに設定を書き込む
void saveConfig();
