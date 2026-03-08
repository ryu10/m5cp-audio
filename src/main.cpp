// 音声入力を SD カードに保存する（デュアルコア版）
//
// アーキテクチャ:
//   Core 1 (mic_task): i2s_read() → ping-pong buf[0/1] → FreeRTOS Queue通知 → stop check
//   Core 0 (loop)    : Queue受信 → file.write(buf) → 波形描画（間引き）→ M5.update() → BtnA check
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
// #define USE_PCM1808  // PCM1808 外部ADCを使う場合は有効化。無効時は内蔵マイクを使用。

// ── SD SPI ピン ──────────────────────────────────────────────
#define SD_SPI_SCK_PIN  (40)
#define SD_SPI_MISO_PIN (39)
#define SD_SPI_MOSI_PIN (14)
#define SD_SPI_CS_PIN   (12)

// ── 録音パラメータ ────────────────────────────────────────────
static constexpr uint32_t SAMPLE_RATE    = 16000;
static constexpr size_t   CHUNK_SAMPLES  = 512;   // 1チャンクのサンプル数（約32ms @16kHz）
static constexpr size_t   NUM_BUFFERS    = 2;     // ピンポンバッファ数

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
static volatile bool is_recording  = false;
static volatile bool stop_requested = false;

// ── SD 書き込みファイル ───────────────────────────────────────
static File     rec_file;
static uint32_t rec_total_samples = 0;  // 録音済みサンプル数（WAVヘッダ更新用）
static uint32_t file_counter      = 0;

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
void mic_task(void* arg);
bool openRecFile();
void finalizeRecFile();
void drawWaveform(const int16_t* buf, size_t len);
void showStatus(const char* label, uint16_t color);

// ============================================================
// mic_task  ―  Core 1
//   役割: ADC/I2S から音声を読み取り、ピンポンバッファに格納して
//         Core 0 へ Queue 通知する。stop_requested を監視して終了。
// ============================================================
void mic_task(void* arg)
{
	uint8_t buf_idx = 0;

	while (true) {
		if (!is_recording) {
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}

		// ここに ADC 読み取りを実装 ─────────────────────────────
		// 【PCM1808使用時】
		//   lineIn.read(ping_pong[buf_idx], CHUNK_SAMPLES);
		// 【内蔵マイク使用時】
		//   M5Cardputer.Mic.record(ping_pong[buf_idx], CHUNK_SAMPLES, SAMPLE_RATE);
		// ─────────────────────────────────────────────────────────

		// 書き込み完了バッファのインデックスを Core0 へ通知
		xQueueSend(rec_queue, &buf_idx, portMAX_DELAY);

		// ピンポン切り替え
		buf_idx ^= 1;

		// 停止チェック
		if (stop_requested) {
			stop_requested = false;
			is_recording   = false;
			// 終了シグナルを Core0 へ送る
			uint8_t sig = QUEUE_STOP_SIGNAL;
			xQueueSend(rec_queue, &sig, portMAX_DELAY);
		}
	}
}

// ============================================================
// openRecFile  ―  録音開始時に WAV ファイルをオープンし
//                 ダミーヘッダを書き込む
// ============================================================
bool openRecFile()
{
	char filename[32];
	snprintf(filename, sizeof(filename), "/rec%04lu.wav", file_counter++);

	rec_file = SD.open(filename, FILE_WRITE);
	if (!rec_file) {
		printf("Failed to open: %s\n", filename);
		return false;
	}

	// ここにダミー WAV ヘッダの書き込みを実装 ──────────────────
	// WAVHeader hdr;  // dataSize=0 のまま書き込み
	// rec_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WAVHeader));
	// ────────────────────────────────────────────────────────────

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

	// ここに WAV ヘッダの上書きを実装 ───────────────────────────
	// uint32_t dataBytes = rec_total_samples * sizeof(int16_t);
	// WAVHeader hdr;
	// hdr.dataSize = dataBytes;
	// hdr.fileSize = 36 + dataBytes;
	// rec_file.seek(0);
	// rec_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WAVHeader));
	// ────────────────────────────────────────────────────────────

	rec_file.close();
	printf("Finalized. Total samples: %lu\n", rec_total_samples);
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

// ============================================================
// showStatus  ―  ディスプレイに録音状態を表示
// ============================================================
void showStatus(const char* label, uint16_t color)
{
	// ここにステータス表示を実装 ─────────────────────────────────
	// 例: M5Cardputer.Display.fillCircle(70, 15, 8, color);
	//     M5Cardputer.Display.drawString(label, 120, 3);
	// ────────────────────────────────────────────────────────────
	(void)label;
	(void)color;
}

// ============================================================
// setup  ―  Core 0
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

	// mic_task を Core 1 に固定して起動
	xTaskCreatePinnedToCore(
		mic_task,   // タスク関数
		"mic_task", // タスク名
		4096,       // スタックサイズ (bytes)
		nullptr,    // 引数
		2,          // 優先度（loop より高め）
		nullptr,    // タスクハンドル（不要なら nullptr）
		1           // Core 1
	);

	showStatus("Ready", WHITE);
	M5Cardputer.Display.endWrite();
}

// ============================================================
// loop  ―  Core 0
//   役割: BtnA で録音開始/停止トグル。
//         Queue からバッファを受け取り SD に書き込む。
//         波形描画、ディスプレイ更新はここで行う。
// ============================================================
void loop(void)
{
	M5Cardputer.update();

	// BtnA: 録音開始 / 停止トグル
	if (M5Cardputer.BtnA.wasClicked()) {
		if (!is_recording) {
			// ── 録音開始 ─────────────────────────────────────────
			if (openRecFile()) {
				is_recording = true;
				showStatus("REC", RED);
				printf("Recording started.\n");
			}
		} else {
			// ── 録音停止要求 ──────────────────────────────────────
			stop_requested = true;
			printf("Stop requested.\n");
		}
	}

	// ── Queue からチャンクを受信して処理 ─────────────────────────
	uint8_t buf_idx;
	if (rec_queue && xQueueReceive(rec_queue, &buf_idx, 0) == pdTRUE) {

		if (buf_idx == QUEUE_STOP_SIGNAL) {
			// 録音終了シグナル受信
			finalizeRecFile();
			showStatus("Done", GREEN);
			// ここに完了後のファイル一覧更新などを実装

		} else {
			// SD にバッファを書き込む
			// ここに SD 書き込みを実装 ─────────────────────────────
			// rec_file.write(
			//     reinterpret_cast<const uint8_t*>(ping_pong[buf_idx]),
			//     CHUNK_SAMPLES * sizeof(int16_t));
			// rec_total_samples += CHUNK_SAMPLES;
			// ────────────────────────────────────────────────────────

			// 波形描画（間引き）
			drawWaveform(ping_pong[buf_idx], CHUNK_SAMPLES);
		}
	}

	// ここにキーボード操作（ファイル選択・再生・削除）を実装
}
