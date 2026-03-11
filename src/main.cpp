// 音声入力を SD カードに保存する（デュアルコア版）
//
// アーキテクチャ:
//   Core 1 (loop)   : i2s_read() → ping-pong buf[0/1] → FreeRTOS Queue通知 → M5.update() → BtnA/stop check
//   Core 0 (sd_task): Queue受信 → file.write(buf) → 波形描画（間引き）
// ※ setup()/loop() は ESP32 Arduino のデフォルトで Core 1 (loopTask) で動作する
//
// 最大録音サイズ: 16000サンプル/秒 × 2バイト × 3600秒 ≒ 115MB
// RAM使用量を抑えるため、SDへの書き込みは録音中にリアルタイムで行う。
// WAVヘッダは録音開始時にダミー値で書き込み、終了時に実際のサイズで上書きする。
// 波形描画: 100サンプルに1点程度に間引いてCPU負荷を抑える。

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "pcm1808.h"
#include "ui.h"
#define USE_PCM1808  // PCM1808 外部ADCを使う場合は有効化。無効時は内蔵マイクを使用。

// ── SD SPI ピン ──────────────────────────────────────────────
#define SD_SPI_SCK_PIN  (40)
#define SD_SPI_MISO_PIN (39)
#define SD_SPI_MOSI_PIN (14)
#define SD_SPI_CS_PIN   (12)

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

// ── 再生状態 ─────────────────────────────────────────────────
static volatile bool is_playing     = false;
static volatile bool play_requested = false;  // sd_task → loop() への再生開始通知

// ── 再生用バッファ・ファイル ──────────────────────────────────
static int16_t play_buf[CHUNK_SAMPLES * 4];  // 再生チャンクバッファ（2048サンプル）
static File    play_file;

// ── SD 書き込みファイル ───────────────────────────────────────
static File     rec_file;
static uint32_t rec_total_samples = 0;  // 録音済みサンプル数（WAVヘッダ更新用）
static uint32_t file_counter      = 0;
static char     filename[32];     // 録音ファイル名格納用

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
//         波形描画もここで行う。QUEUE_STOP_SIGNAL を受けたら
//         WAV ヘッダを確定してファイルをクローズする。
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
			play_requested = true;  // loop() に再生開始を通知
		} else {
			// SD にバッファを書き込む
			rec_file.write(
			    reinterpret_cast<const uint8_t*>(ping_pong[buf_idx]),
			    CHUNK_SAMPLES * sizeof(int16_t));
			rec_total_samples += CHUNK_SAMPLES;

			// 波形描画
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
	// char filename[32];
	snprintf(filename, sizeof(filename), "/rec%04lu.wav", file_counter); 

	// もし同名ファイルがあったら file_counter をインクリメントしてユニークな名前にする
	// 上限は 0999 までとする
	while (SD.exists(filename) && file_counter < 1000) {
		file_counter++;
		snprintf(filename, sizeof(filename), "/rec%04lu.wav", file_counter);
	}
	// 1000 に達したらエラーを表示して録音開始を諦める
	if (file_counter >= 1000) {
		printf("Too many files. Cannot create new recording.\n");
		// ここにエラー表示を実装
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

	// WAV ヘッダの上書
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
	// I2S ADC を停止してから Speaker を起動（I2S_NUM_1 → I2S_NUM_0 の順で解放）
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
	M5Cardputer.Speaker.setVolume(200);

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
				// チャンク供給ごとに VU メーター更新（自然に ~8fps = 2048samples/16kHz）
				drawVUMeter(computeRMS(play_buf, n / sizeof(int16_t)));
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
		// ここにエラー表示を実装
		while (1) { delay(1); }
	}
	printf("SD OK - %lluMB\r\n", SD.cardSize() / (1024 * 1024));

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

	showStatus("Ready", WHITE, "", "Press any key to record");
}

// ============================================================
// loop  ―  Core 1（ESP32 Arduino のデフォルト）
//   役割: ADC 読み取り → ピンポンバッファへ格納 → Queue 通知
//         M5.update() → BtnA または任意キーで状態遷移
//           再生中  → 再生停止して入力待ちへ
//           入力待ち → 録音開始
//           録音中  → 録音停止要求
// ============================================================
void loop(void)
{
	M5Cardputer.update();

	const bool trigger = M5Cardputer.BtnA.wasClicked() ||
	                     (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed());
	if (trigger) {
		if (is_playing) {
			// ── 再生停止 ──────────────────────────────────────────
			M5Cardputer.Speaker.stop();
			M5Cardputer.Speaker.end();
			play_file.close();
			is_playing = false;
			showStatus("Ready", WHITE, "", "Press any key to record");
			printf("Playback stopped by user.\n");
		} else if (!is_recording) {
			// ── 録音開始 ─────────────────────────────────────────
			if (openRecFile()) {
				is_recording = true;
#ifdef USE_PCM1808
				// 再生時に end() した I2S ADC を再初期化
				lineIn.begin(3, 4, 5, 13);
#endif
				resetVUMeter();
				showStatus("REC", RED, "", "Press any key to stop");
				drawVUMeter(0.0f);
				printf("Recording started.\n");
			} else {
				printf("Failed to start recording.\n");
				// ここにエラー表示を実装
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

	// ── 再生フロー ───────────────────────────────────────────────
	// sd_task が finalizeRecFile() 完了後に play_requested をセット
	if (play_requested && !is_playing) {
		play_requested = false;
		is_playing     = true;
		showStatus("PLAY", BLUE, filename, "Press any key to stop");
		resetVUMeter();
		startPlayback(filename);
		// 開始直後に t=0 のインジケーター・VU メーターを即時描画
		drawTimeIndicator(0, rec_total_samples / SAMPLE_RATE);
		drawVUMeter(0.0f);
		printf("Playback started: %s\n", filename);
	}

	// 再生完了 → 初期状態に戻る
	if (is_playing && isPlaybackDone()) {
		is_playing = false;
		showStatus("Ready", WHITE, "", "Press any key to record");
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

	// ── ADC 読み取り → ピンポンバッファへ格納 → Queue 通知 ──────
	static uint8_t buf_idx = 0;

	// ADC 読み取り
#ifdef USE_PCM1808
	lineIn.read(ping_pong[buf_idx], CHUNK_SAMPLES);
#else
	M5Cardputer.Mic.record(ping_pong[buf_idx], CHUNK_SAMPLES, SAMPLE_RATE);
#endif

	// 書き込み完了バッファのインデックスを Core 0 (sd_task) へ通知
	// VU メーター更新: Queue 送信前の今が buf_idx に sd_task が触れていない唯一の瞬間
	drawVUMeter(computeRMS(ping_pong[buf_idx], CHUNK_SAMPLES));
	if (rec_queue) xQueueSend(rec_queue, &buf_idx, 0);

	// ピンポン切り替え
	buf_idx ^= 1;

	// 停止チェック
	if (stop_requested) {
		stop_requested = false;
		is_recording   = false;
		// 終了シグナルを Core 0 (sd_task) へ送る
		if (rec_queue) {
			uint8_t sig = QUEUE_STOP_SIGNAL;
			xQueueSend(rec_queue, &sig, portMAX_DELAY);
		}
	}

}
