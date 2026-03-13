// 音声入力を SD カードに録音・再生するアプリ（デュアルコア版）
//
// アーキテクチャ:
//   Core 1 (loop)   : i2s_read() → ping-pong buf[0/1] → FreeRTOS Queue 通知
//                     M5.update() → キー/BtnA 判定 → 状態遷移
//   Core 0 (sd_task): Queue 受信 → SD write → VU メーター更新
//                     録音終了シグナル受信 → WAV ヘッダ確定 → 再生通知
// ※ setup()/loop() は ESP32 Arduino のデフォルトで Core 1 (loopTask) で動作する
//
// 最大録音サイズ: 16000サンプル/秒 × 2バイト × 3600秒 ≒ 115MB
// RAM使用量を抑えるため、SDへの書き込みは録音中にリアルタイムで行う。
// WAVヘッダは録音開始時にダミー値で書き込み、終了時に実際のサイズで上書きする。
//
// 画面状態: Ready ←→ Browse / REC → PLAY → Ready

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config.h"
#include "pcm1808.h"
#include "ui.h"
#define USE_PCM1808  // PCM1808 外部ADCを使う場合は有効化。無効時は内蔵マイクを使用。

// ── SD SPI ピン ──────────────────────────────────────────────
#define SD_SPI_SCK_PIN  (40)
#define SD_SPI_MISO_PIN (39)
#define SD_SPI_MOSI_PIN (14)
#define SD_SPI_CS_PIN   (12)

// ── RMT スイッチ ─────────────────────────────────────────────
#define RMT_SWITCH_PIN     (6)    // ADV G6: INPUT_PULLUP / 通常 H、スイッチ ON で L
#define RMT_DEBOUNCE_MS    (80u)  // リレーバウンス対策デバウンス時間（ms）

// ── 録音パラメータ ────────────────────────────────────────────
static constexpr uint32_t SAMPLE_RATE    = 16000;
static constexpr size_t   CHUNK_SAMPLES  = 512;   // 1チャンクのサンプル数（約32ms @16kHz）
static constexpr size_t   NUM_BUFFERS    = 2;     // ピンポンバッファ数

#define MAX_RECORDING_SIZE (SAMPLE_RATE * sizeof(int16_t) * 3600)  // 最大録音サイズ（1時間分）

// ── ピンポンバッファ ──────────────────────────────────────────
// Core1 が書き込み、Core0 が読み出す。
// Queue でどちらのバッファが「書き込み完了」かを通知する。
static int16_t ping_pong[NUM_BUFFERS][CHUNK_SAMPLES];

// ── Queue: Core1 → Core0 ─────────────────────────────────────
// 要素: uint8_t（バッファインデックス 0 or 1）
// 特殊値 0xFF を「録音終了」シグナルとして使う
static QueueHandle_t rec_queue = nullptr;
static constexpr uint8_t QUEUE_STOP_SIGNAL = 0xFF;

// ── 録音状態 ─────────────────────────────────────────────────
static volatile bool is_recording   = false;
static volatile bool stop_requested = false;

// ── ブラウザ状態 ──────────────────────────────────────────────
static bool is_browsing = false;

// ── RMT 待機状態 ──────────────────────────────────────────────
static bool rmt_waiting = false;  // RMT モードで G6=L 待ち中

// ── 設定画面状態 ─────────────────────────────────────────────
static bool is_settings = false;  // 設定画面表示中

// ── RMT デバウンス状態 ───────────────────────────────────────
static int      rmt_raw_prev   = HIGH;  // 前回の RAW 読み値
static uint32_t rmt_edge_ms    = 0;     // エッジ検出時刻
static int      rmt_debounced  = HIGH;  // デバウンス済み確定値

// ── 再生状態 ─────────────────────────────────────────────────
static volatile bool is_playing     = false;
static volatile bool play_requested = false;  // sd_task → loop() への再生開始通知

// ── 再生用バッファ・ファイル ──────────────────────────────────
static int16_t play_buf[CHUNK_SAMPLES * 2];  // 再生チャンクバッファ（1024サンプル, ~64ms → ~16fps VU 更新）
static File    play_file;

// ── SD 書き込みファイル ───────────────────────────────────────
static File     rec_file;
static uint32_t rec_total_samples = 0;  // 録音済みサンプル数（WAVヘッダ更新用）
static uint32_t file_counter      = 0;
static char     filename[32];     // 録音ファイル名格納用
static char     current_file[32]; // カレントファイル名

// ── WAVヘッダ ─────────────────────────────────────────────────
struct WAVHeader {
	char     riff[4]        = {'R', 'I', 'F', 'F'};
	uint32_t fileSize       = 0;           // 録音終了後に書き直す
	char     wave[4]        = {'W', 'A', 'V', 'E'};
	char     fmt[4]         = {'f', 'm', 't', ' '};
	uint32_t fmtSize        = 16;
	uint16_t audioFormat    = 1;           // PCM
	uint16_t numChannels    = 1;
	uint32_t sampleRate     = SAMPLE_RATE;
	uint32_t byteRate       = SAMPLE_RATE * sizeof(int16_t);
	uint16_t blockAlign     = sizeof(int16_t);
	uint16_t bitsPerSample  = 16;
	char     data[4]        = {'d', 'a', 't', 'a'};
	uint32_t dataSize       = 0;           // 録音終了後に書き直す
};

// ── PCM1808 ───────────────────────────────────────────────────
#ifdef USE_PCM1808
PCM1808 lineIn;
#endif

// ── 前方宣言 ──────────────────────────────────────────────────
void sd_task(void* arg);
bool openRecFile();
void finalizeRecFile();
void startPlayback(const char* fname);
bool isPlaybackDone();

// ============================================================
// sd_task  ―  Core 0
//   役割: Queue からバッファインデックスを受け取り SD に書き込む。
//         VU メーター更新もここで行う。QUEUE_STOP_SIGNAL を受けたら
//         WAV ヘッダを確定・クローズし、カレントファイルを更新して
//         loop() に再生開始を通知する。
// ============================================================
void sd_task(void* arg)
{
	while (true) {
		uint8_t buf_idx;
		// Queue を待機（portMAX_DELAY でブロック）
		if (xQueueReceive(rec_queue, &buf_idx, portMAX_DELAY) != pdTRUE) continue;

		if (buf_idx == QUEUE_STOP_SIGNAL) {
			// 録音終了シグナル受信 → WAV ヘッダ確定
			finalizeRecFile();
			strncpy(current_file, filename, sizeof(current_file));
			// 設定ファイルに保存
			strncpy(g_config.current_file, current_file, sizeof(g_config.current_file));
			saveConfig();
			play_requested = true;  // loop() に再生開始を通知
		} else {
			// SD にバッファを書き込む
			rec_file.write(
			    reinterpret_cast<const uint8_t*>(ping_pong[buf_idx]),
			    CHUNK_SAMPLES * sizeof(int16_t));
			rec_total_samples += CHUNK_SAMPLES;

			// VU メーター更新（REC 画面）
			drawWaveform(ping_pong[buf_idx], CHUNK_SAMPLES);
		}
	}
}

// ============================================================
// openRecFile  ―  録音開始時に WAV ファイルをオープンし
//                 ダミーヘッダを書き込む
// ============================================================
bool openRecFile()
{
	snprintf(filename, sizeof(filename), "/rec%04lu.wav", file_counter);

	// 同名ファイルがあれば file_counter をインクリメントしてユニークな名前にする（上限 0999）
	while (SD.exists(filename) && file_counter < 1000) {
		file_counter++;
		snprintf(filename, sizeof(filename), "/rec%04lu.wav", file_counter);
	}
	if (file_counter >= 1000) {
		printf("Too many files. Cannot create new recording.\n");
		return false;
	}

	rec_file = SD.open(filename, FILE_WRITE);
	if (!rec_file) {
		printf("Failed to open: %s\n", filename);
		return false;
	}

	// ダミー WAV ヘッダの書き込み
	WAVHeader hdr;  // dataSize=0 のまま書き込み
	rec_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WAVHeader));

	rec_total_samples = 0;
	printf("Opened: %s\n", filename);
	return true;
}

// ============================================================
// finalizeRecFile  ―  録音終了時に WAV ヘッダを実サイズで上書き
// ============================================================
void finalizeRecFile()
{
	if (!rec_file) return;

	// WAV ヘッダを上書き（dataSize / fileSize を実サイズで確定）
	uint32_t dataBytes = rec_total_samples * sizeof(int16_t);
	WAVHeader hdr;
	hdr.dataSize = dataBytes;
	hdr.fileSize = 36 + dataBytes;
	rec_file.seek(0);
	rec_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WAVHeader));

	rec_file.close();
	printf("Finalized. Total samples: %lu\n", rec_total_samples);
}

// ============================================================
// startPlayback  ―  WAV ファイルをスピーカーで再生開始する
// ============================================================
void startPlayback(const char* fname)
{
	// PCM1808 I2S ADC を停止してからスピーカー I2S を起動（リソース競合を避けるため）
#ifdef USE_PCM1808
	lineIn.end();
#endif

	play_file = SD.open(fname, FILE_READ);
	if (!play_file) {
		printf("startPlayback: failed to open %s\n", fname);
		is_playing = false;
		return;
	}

	// WAV データ部先頭（ヘッダ 44 bytes = sizeof(WAVHeader)）へシーク
	play_file.seek(sizeof(WAVHeader));

	M5Cardputer.Speaker.begin();
	M5Cardputer.Speaker.setVolume(g_config.speaker_volume);

	// 最初のチャンクを読み込んで再生開始
	size_t n = play_file.read(reinterpret_cast<uint8_t*>(play_buf), sizeof(play_buf));
	if (n > 0) {
		M5Cardputer.Speaker.playRaw(play_buf, n / sizeof(int16_t),
		                            SAMPLE_RATE, /*stereo=*/false,
		                            /*repeat=*/1, /*ch=*/0, /*stop=*/true);
	} else {
		play_file.close();
		M5Cardputer.Speaker.end();
		is_playing = false;
	}
}

// ============================================================
// computeRMS  ―  バッファの RMS 音量を 0.0〜1.0 で返す
// ============================================================
static float computeRMS(const int16_t* buf, size_t samples)
{
	if (samples == 0) return 0.0f;
	uint64_t sum = 0;
	for (size_t i = 0; i < samples; i++) {
		int32_t s = buf[i];
		sum += (uint64_t)(s * s);
	}
	return sqrtf((float)sum / (float)samples) / 32768.0f;
}

// ============================================================
// isPlaybackDone  ―  再生完了を判定し、途中チャンクを供給する
// ============================================================
bool isPlaybackDone()
{
	if (!M5Cardputer.Speaker.isPlaying()) {
		if (play_file && play_file.available()) {
			// 次のチャンクを供給
			size_t n = play_file.read(reinterpret_cast<uint8_t*>(play_buf), sizeof(play_buf));
			if (n > 0) {
				M5Cardputer.Speaker.playRaw(play_buf, n / sizeof(int16_t),
				                            SAMPLE_RATE, /*stereo=*/false,
				                            /*repeat=*/1, /*ch=*/0, /*stop=*/false);
				// チャンク供給ごとに VU メーター更新
			// 1024サンプルを 512×2 に分割して REC 時（512サンプル）と EMA 刻みを揃える
			const size_t samples = n / sizeof(int16_t);
			const size_t half    = samples / 2;
			drawVUMeter(computeRMS(play_buf,        half));
			drawVUMeter(computeRMS(play_buf + half, samples - half));
				return false;
			}
		}
		// ファイル末尾まで再生完了
		play_file.close();
		M5Cardputer.Speaker.end();
		return true;
	}
	return false;
}

// ============================================================
// setup  ―  Core 1（ESP32 Arduino のデフォルト）
// ============================================================
void setup(void)
{
	auto cfg = M5.config();
	M5Cardputer.begin(cfg);
	Serial.begin(115200);

	M5Cardputer.Display.startWrite();
	M5Cardputer.Display.setRotation(1);
	M5Cardputer.Display.setTextDatum(top_center);
	M5Cardputer.Display.setTextColor(WHITE);
	M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);

	// SD カード初期化
	SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
	if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
		printf("SD init failed\r\n");
		while (1) { delay(1); }
	}
	printf("SD OK - %lluMB\r\n", SD.cardSize() / (1024 * 1024));

	// 設定ファイル読み込み（なければデフォルト値で新規作成）
	loadConfig();

	// カレントファイル初期化（設定ファイルの値を使用）
	strncpy(current_file, g_config.current_file, sizeof(current_file));

	// RMT スイッチ入力設定（GPIO6: プルアップ = 通常 H）
	pinMode(RMT_SWITCH_PIN, INPUT_PULLUP);

	// ADC 初期化
#ifdef USE_PCM1808
	// PCM1808 Line In  (EXT port: BCK=3, LRCK=4, DIN=5, MCK=13)
	lineIn.begin(3, 4, 5, 13);
#else
	M5Cardputer.Mic.begin();
#endif

	// FreeRTOS Queue 作成（深さ NUM_BUFFERS+1: ストップシグナル分の余裕を持たせる）
	rec_queue = xQueueCreate(NUM_BUFFERS + 1, sizeof(uint8_t));

	// sd_task を Core 0 に固定して起動
	xTaskCreatePinnedToCore(
		sd_task,   // タスク関数
		"sd_task", // タスク名
		4096,      // スタックサイズ (bytes)
		nullptr,   // 引数
		1,         // 優先度
		nullptr,   // タスクハンドル（不要なら nullptr）
		0          // Core 0
	);

	showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
}

// ============================================================
// loop  ―  Core 1（ESP32 Arduino のデフォルト）
//   役割: M5.update() → キー/BtnA 入力解析 → 画面状態に応じた遷移
//
//   Ready 状態:
//     F キー          → Browse（ファイルブラウザ）へ
//     S キー          → Settings（設定画面）へ
//     R キー / BtnA   → REC（新規録音）へ
//     その他のキー    → PLAY（カレントファイルを再生）へ
//   Browse 状態:
//     ; / .           → リスト上下移動
//     ` (Esc)         → カレントファイル変更なしで Ready へ
//     その他 / Enter  → 選択ファイルをカレントにして Ready へ
//   Settings 状態:
//     ; / .           → フォーカス上下移動
//     , / /           → 設定値切り替え（Yes/No）
//     ` (Esc)         → キャンセル（変更破棄）して Ready へ
//     Enter / その他  → 変更を保存して Ready へ
//   REC 状態:
//     任意キー        → 録音停止要求（Core 0 が WAV 確定後 PLAY へ）
//   PLAY 状態:
//     任意キー        → 再生停止して Ready へ
// ============================================================
void loop(void)
{
	M5Cardputer.update();

	// RMT スイッチ デバウンス（毎ループ先頭で 1 回だけ読む）
	if (g_config.use_rmt) {
		const int raw = digitalRead(RMT_SWITCH_PIN);
		if (raw != rmt_raw_prev) {
			rmt_raw_prev = raw;
			rmt_edge_ms  = millis();
		}
		if ((millis() - rmt_edge_ms) >= RMT_DEBOUNCE_MS) {
			rmt_debounced = raw;
		}
	}

	// キー入力を解析
	char pressedChar  = 0;
	bool pressedEnter = false;
	if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
		const auto& ks = M5Cardputer.Keyboard.keysState();
		const auto& w  = ks.word;
		if (!w.empty()) pressedChar = w[0];
		pressedEnter = ks.enter;
	}
	const bool trigger   = M5Cardputer.BtnA.wasClicked() || (pressedChar != 0) || pressedEnter;
	const bool trigger_f = (pressedChar == 'f' || pressedChar == 'F');
	const bool trigger_r = M5Cardputer.BtnA.wasClicked() ||
	                       (pressedChar == 'r' || pressedChar == 'R');
	const bool trigger_s = (pressedChar == 's' || pressedChar == 'S');

	if (trigger) {
		if (is_playing) {
			// ── 再生停止 ──────────────────────────────────────────
			M5Cardputer.Speaker.stop();
			M5Cardputer.Speaker.end();
			play_file.close();
			is_playing = false;
			showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
			printf("Playback stopped by user.\n");
		} else if (is_browsing) {
			if (pressedChar == ';') {
				// ── ブラウザ: 上に移動 ─────────────────────────────
				browseMoveUp();
				showFileBrowser();
			} else if (pressedChar == '.') {
				// ── ブラウザ: 下に移動 ─────────────────────────────
				browseMoveDown();
				showFileBrowser();
			} else if (pressedChar == '`') {
				// ── ブラウザ → Ready（Esc: 選択変更なし）──────────
				is_browsing = false;
				showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
				printf("Browse cancelled.\n");
			} else {
				// ── ブラウザ → Ready（決定: カレントファイル更新）──
				const char* sel = browseSelectedFile();
				if (sel) {
					snprintf(current_file, sizeof(current_file), "/%s", sel);
					// 設定ファイルに保存
					strncpy(g_config.current_file, current_file, sizeof(g_config.current_file));
					saveConfig();
				}
				is_browsing = false;
				showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
				printf("Browse selected: %s\n", current_file);
			}
		} else if (rmt_waiting) {
			// ── RMT 待機キャンセル ─────────────────────────────────
			rmt_waiting = false;
			showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
			printf("RMT wait cancelled.\n");
		} else if (is_settings) {
			// ── 設定画面: キー入力処理 ──────────────────────────────
			if (pressedChar == ';') {
				settingsMoveUp();
				showSettingsScreen();
			} else if (pressedChar == '.') {
				settingsMoveDown();
				showSettingsScreen();
			} else if (pressedChar == ',') {
				settingsChange(false);
				showSettingsScreen();
			} else if (pressedChar == '/') {
				settingsChange(true);
				showSettingsScreen();
			} else if (pressedChar == '`') {
				// ── 設定キャンセル（変更破棄）─────────────────────────
				is_settings = false;
				showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
				printf("Settings cancelled.\n");
			} else {
				// ── 設定保存して閉じる ──────────────────────────────
				if (settingsCommit()) saveConfig();
				is_settings = false;
				showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
				printf("Settings saved.\n");
			}
		} else if (!is_recording) {
			if (trigger_f) {
				// ── ファイルブラウザへ遷移 ────────────────────────
				is_browsing = true;
				initFileBrowser(current_file);
				showFileBrowser();
				printf("File browser opened.\n");			} else if (trigger_s) {
				// ── 設定画面へ遷移 ──────────────────────────────────────────
				is_settings = true;
				initSettingsScreen();
				showSettingsScreen();
				printf("Settings opened.\n");			} else if (trigger_r) {
				// ── 新規録音開始 ──────────────────────────────────
				if (g_config.use_rmt && rmt_debounced == HIGH) {
					// RMT モード: G6=H（プラグまた・スイッチ OFF）→ G6=L 待機
					rmt_waiting = true;
					showStatus("RMT Wait", YELLOW, "", "Waiting for switch  [any key] Cancel");
					printf("RMT: G6=H, waiting for G6=L.\n");
				} else if (openRecFile()) {
					// RMT モードなら G6=L（プラグ未挑叁またはスイッチ ON）→ 即座録音開始
					is_recording = true;
#ifdef USE_PCM1808
					// 録音開始後 I2S ADC を再初期化（PLAY 時に lineIn.end() しているため）
					lineIn.begin(3, 4, 5, 13);
#endif
					resetVUMeter();
					showStatus("REC", RED, "", "Press any key to stop");
					drawVUMeter(0.0f);
					printf("Recording started%s.\n", g_config.use_rmt ? " (RMT G6=L)" : "");
				} else {
					printf("Failed to start recording.\n");
				}
			} else {
				// ── カレントファイルを再生 ────────────────────────
				if (current_file[0] == '\0' || !SD.exists(current_file)) {
					showStatus("No file", YELLOW, "(none)", "[R] Rec  [F] Browse");
					printf("No playable file.\n");
				} else {
					uint32_t total_samples = 0;
					{
						File f = SD.open(current_file, FILE_READ);
						if (f) {
							WAVHeader hdr;
							f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(WAVHeader));
							total_samples = hdr.dataSize / sizeof(int16_t);
							f.close();
						}
					}
					rec_total_samples = total_samples;
					is_playing = true;
					showStatus("PLAY", BLUE, current_file, "Press any key to stop");
					resetVUMeter();
					startPlayback(current_file);
					drawTimeIndicator(0, total_samples / SAMPLE_RATE);
					drawVUMeter(0.0f);
					printf("Playing: %s\n", current_file);
				}
			}
		} else {
			// ── 録音停止要求 ──────────────────────────────────────
			stop_requested = true;
			printf("Stop requested.\n");
		}
	}

	// 録音最大サイズに達した場合はボタン検知せずに録音停止
	if (is_recording && rec_total_samples >= (MAX_RECORDING_SIZE / sizeof(int16_t))) {
		stop_requested = true;
		printf("Max recording size reached. Stop requested.\n");
	}

	// RMT モード: G6=H に戻ったら録音停止
	// （キー入力による停止は既存の stop_requested フローで処理済み）
	if (is_recording && g_config.use_rmt && rmt_debounced == HIGH) {
		stop_requested = true;
		printf("RMT: G6 returned HIGH. Stop requested.\n");
	}

	// ── 録音後の自動再生フロー ─────────────────────────────────────
	// sd_task が WAV 確定後に play_requested をセットする
	if (play_requested && !is_playing) {
		play_requested = false;
		is_playing     = true;
		showStatus("PLAY", BLUE, filename, "Press any key to stop");
		resetVUMeter();
		startPlayback(filename);
		drawTimeIndicator(0, rec_total_samples / SAMPLE_RATE);
		drawVUMeter(0.0f);
		printf("Playback started: %s\n", filename);
	}

	// 再生完了 → 初期状態に戻る
	if (is_playing && isPlaybackDone()) {
		is_playing = false;
		showStatus("Ready", WHITE, current_file, "[F]Browse [R]Rec [S]Set [key]Play");
		printf("Playback finished. Ready.\n");
	}

	// 再生中タイムインジケーター・棒グラフを 1 秒ごとに更新
	if (is_playing) {
		static uint32_t last_ui_ms = 0;
		const uint32_t now = millis();
		if (now - last_ui_ms >= 1000) {
			last_ui_ms = now;
			const uint32_t played_bytes =
				(play_file.position() > sizeof(WAVHeader))
				? play_file.position() - sizeof(WAVHeader) : 0;
			const uint32_t cur_sec   = played_bytes / (SAMPLE_RATE * sizeof(int16_t));
			const uint32_t total_sec = rec_total_samples / SAMPLE_RATE;
			drawTimeIndicator(cur_sec, total_sec);
		}
	}

	// RMT 待機中: G6=L に変化したら録音開始
	if (rmt_waiting) {
		if (rmt_debounced == LOW) {
			rmt_waiting = false;
			if (openRecFile()) {
				is_recording = true;
#ifdef USE_PCM1808
				lineIn.begin(3, 4, 5, 13);
#endif
				resetVUMeter();
				showStatus("REC", RED, "", "Press any key to stop");
				drawVUMeter(0.0f);
				printf("RMT: Recording started (G6=L).\n");
			}
		}
	}

	if (!is_recording) return;

	// 録音中タイムインジケーターを 1 秒ごとに更新（合計・バーなし）
	{
		static uint32_t rec_ui_ms = 0;
		const uint32_t now = millis();
		if (now - rec_ui_ms >= 1000) {
			rec_ui_ms = now;
			drawTimeIndicator(rec_total_samples / SAMPLE_RATE, 0);
		}
	}

	// ── ADC 読み取り → ピンポンバッファへ書き込み → Queue 通知 ──
	static uint8_t buf_idx = 0;

	// ADC 読み取り
#ifdef USE_PCM1808
	lineIn.read(ping_pong[buf_idx], CHUNK_SAMPLES);
#else
	M5Cardputer.Mic.record(ping_pong[buf_idx], CHUNK_SAMPLES, SAMPLE_RATE);
#endif

	// VU メーター更新（Queue 送信前 = buf_idx に sd_task が触れていない唯一の瞬間）
	drawVUMeter(computeRMS(ping_pong[buf_idx], CHUNK_SAMPLES));
	if (rec_queue) xQueueSend(rec_queue, &buf_idx, 0);

	// ピンポン切り替え
	buf_idx ^= 1;

	// 停止チェック
	if (stop_requested) {
		stop_requested = false;
		is_recording   = false;
		// 録音終了シグナルを Core 0 (sd_task) へ送る → finalizeRecFile() が走る
		if (rec_queue) {
			uint8_t sig = QUEUE_STOP_SIGNAL;
			xQueueSend(rec_queue, &sig, portMAX_DELAY);
		}
	}

}
