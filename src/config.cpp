// SD カードの設定ファイル（INI 形式）を読み書きするモジュール
//
// ファイルパス: /<APP_NAME>_config.ini  (APP_NAME は platformio.ini の prog_name)
// フォーマット例:
//   [app]
//   current_file=/rec0001.wav
//   skip_silence=No
//   use_rmt=No

#include "config.h"

#include <SD.h>

static const char* CONFIG_PATH = "/" APP_NAME "_config.ini";

AppConfig g_config;

// ── デフォルト値を設定 ────────────────────────────────────────
static void setDefaults()
{
    g_config.current_file[0] = '\0';  // 未設定
    g_config.skip_silence  = false;
    g_config.use_rmt       = false;
    g_config.speaker_volume = 128;
}

// ── "Yes"/"True"/"1" → true、それ以外 → false ─────────────────
static bool parseBool(const String& val)
{
    return val.equalsIgnoreCase("yes") ||
           val.equalsIgnoreCase("true") ||
           val == "1";
}

// ============================================================
// loadConfig  ―  SD カードから設定を読み込む
//   ファイルがない・開けない場合はデフォルト値で saveConfig() する
// ============================================================
void loadConfig()
{
    setDefaults();

    if (!SD.exists(CONFIG_PATH)) {
        printf("Config not found. Creating with defaults.\n");
        saveConfig();
        return;
    }

    File f = SD.open(CONFIG_PATH, FILE_READ);
    if (!f) {
        printf("loadConfig: failed to open %s. Using defaults.\n", CONFIG_PATH);
        saveConfig();
        return;
    }

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        // 空行・セクション行・コメント行はスキップ
        if (line.isEmpty() || line[0] == '[' || line[0] == '#' || line[0] == ';') continue;

        int sep = line.indexOf('=');
        if (sep < 0) continue;

        String key = line.substring(0, sep);
        String val = line.substring(sep + 1);
        key.trim();
        val.trim();

        if (key == "current_file") {
            strncpy(g_config.current_file, val.c_str(), sizeof(g_config.current_file) - 1);
            g_config.current_file[sizeof(g_config.current_file) - 1] = '\0';
        } else if (key == "skip_silence") {
            g_config.skip_silence = parseBool(val);
        } else if (key == "use_rmt") {
            g_config.use_rmt = parseBool(val);
        } else if (key == "speaker_volume") {
            int v = val.toInt();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            g_config.speaker_volume = (uint8_t)v;
        }
    }

    f.close();
    printf("Config loaded: file=%s skip_silence=%d use_rmt=%d speaker_volume=%d\n",
           g_config.current_file, g_config.skip_silence, g_config.use_rmt, g_config.speaker_volume);
}

// ============================================================
// saveConfig  ―  現在の設定を SD カードに書き込む
// ============================================================
void saveConfig()
{
    // 既存ファイルを削除してから書き直す
    if (SD.exists(CONFIG_PATH)) SD.remove(CONFIG_PATH);

    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        printf("saveConfig: failed to open %s\n", CONFIG_PATH);
        return;
    }

    f.println("[app]");
    f.print("current_file=");
    f.println(g_config.current_file);
    f.print("skip_silence=");
    f.println(g_config.skip_silence ? "Yes" : "No");
    f.print("use_rmt=");
    f.println(g_config.use_rmt ? "Yes" : "No");
    f.print("speaker_volume=");
    f.println(g_config.speaker_volume);

    f.close();
    printf("Config saved.\n");
}
