// 音声入力を SD カードに保存する
// .pio/libdeps/m5stack-stamps3/M5Cardputer/examples/Basic/mic_wav_record/mic_wav_record.ino を PlatformIO 用に改変

/*
 * SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @Hardwares: M5Cardputer
 * @Platform Version: Arduino M5Stack Board Manager v2.0.7
 * @Dependent Library:
 * M5GFX@^0.2.3: https://github.com/m5stack/M5GFX
 * M5Cardputer@^1.0.3: https://github.com/m5stack/M5Cardputer
 */

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>

#include "pcm1808.h"
#define pcm1808_f // PCM1808 を使用する場合はコメントアウトを外す。M5Cardputer 内蔵マイクを使用する場合はコメントアウトしたままにする。

#include <vector>

#define SD_SPI_SCK_PIN  (40)
#define SD_SPI_MISO_PIN (39)
#define SD_SPI_MOSI_PIN (14)
#define SD_SPI_CS_PIN   (12)

static constexpr size_t record_number     = 512;
static constexpr size_t record_length     = 240;
static constexpr size_t record_size       = record_number * record_length;
static constexpr size_t record_samplerate = 16000;

static int16_t prev_y[record_length];
static int16_t prev_h[record_length];
static size_t rec_record_idx  = 2;
static size_t draw_record_idx = 0;
static int16_t* rec_data      = nullptr;

static uint32_t file_counter     = 0;
static uint8_t selectedFileIndex = 0;
static std::vector<String> wavFiles;

struct WAVHeader {
	char riff[4]           = {'R', 'I', 'F', 'F'};
	uint32_t fileSize      = 0;
	char wave[4]           = {'W', 'A', 'V', 'E'};
	char fmt[4]            = {'f', 'm', 't', ' '};
	uint32_t fmtSize       = 16;
	uint16_t audioFormat   = 1;
	uint16_t numChannels   = 1;
	uint32_t sampleRate    = record_samplerate;
	uint32_t byteRate      = record_samplerate * sizeof(int16_t);
	uint16_t blockAlign    = sizeof(int16_t);
	uint16_t bitsPerSample = 16;
	char data[4]           = {'d', 'a', 't', 'a'};
	uint32_t dataSize      = 0;
};

bool saveWAVToSD(int16_t* data, size_t dataSize);
void scanAndDisplayWAVFiles(void);
bool playWAVFileFromSD(void);
bool playSelectedWAVFile(const String& fileName);
void playWAV(void);
void updateDisplay(const std::vector<String>& files, uint8_t selectedIndex);

// PCM1808 Line In
PCM1808 lineIn;

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

	// SD card initialization.
	SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

	if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
		printf("Card failed, or not present\r\n");
		while (1) {
			delay(1);
		}
	}

	uint8_t cardType = SD.cardType();
	if (cardType == CARD_NONE) {
		printf("No SD card attached\r\n");
		return;
	}

	printf("SD Card Type: ");
	if (cardType == CARD_MMC) {
		printf("MMC\r\n");
	} else if (cardType == CARD_SD) {
		printf("SDSC\r\n");
	} else if (cardType == CARD_SDHC) {
		printf("SDHC\r\n");
	} else {
		printf("UNKNOWN\r\n");
	}

	uint64_t cardSize = SD.cardSize() / (1024 * 1024);
	printf("SD Card Size: %lluMB\r\n", cardSize);

	rec_data = static_cast<int16_t*>(
		heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT));
	if (!rec_data) {
		printf("Failed to allocate recording buffer\r\n");
		return;
	}

	memset(rec_data, 0, record_size * sizeof(int16_t));
	M5Cardputer.Speaker.setVolume(255);
	M5Cardputer.Speaker.end();
#ifdef pcm1808_f
  // PCM1808 Line In setup
  // Cardputer EXT port - BCK: 3, LRCK: 4, DIN: 5, MCK: 13
  lineIn.begin(3, 4, 5, 13);
#else
	M5Cardputer.Mic.begin();
#endif

	scanAndDisplayWAVFiles();
	updateDisplay(wavFiles, selectedFileIndex);
}

void loop(void)
{
	M5Cardputer.update();

	// Press BtnA to record and then save to SD.
	if (M5Cardputer.BtnA.wasClicked()) {
		M5Cardputer.Display.clear();

#ifdef pcm1808_f
    if(lineIn.isEnabled() && rec_data) {
#else
 		if (M5Cardputer.Mic.isEnabled() && rec_data) {
#endif
      M5Cardputer.Display.fillCircle(70, 15, 8, RED);
			M5Cardputer.Display.drawString("REC", 120, 3);

			static constexpr int shift = 6;
			for (uint16_t i = 0; i < record_number; i++) {
				auto data = &rec_data[i * record_length];

#ifdef pcm1808_f
        lineIn.read(data, record_length);
        if(true) { // PCM1808 は動機読み取り
#else
        if (M5Cardputer.Mic.record(data, record_length, record_samplerate)) {
#endif
          if (i >= 2) {
						data      = &rec_data[(i - 2) * record_length];
						int32_t w = M5Cardputer.Display.width();
						if (w > static_cast<int32_t>(record_length - 1)) {
							w = record_length - 1;
						}

						for (int32_t x = 0; x < w; ++x) {
							M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], TFT_BLACK);
							int32_t y1 = (data[x] >> shift);
							int32_t y2 = (data[x + 1] >> shift);
							if (y1 > y2) {
								int32_t tmp = y1;
								y1          = y2;
								y2          = tmp;
							}
							int32_t y = (M5Cardputer.Display.height() >> 1) + y1;
							int32_t h = (M5Cardputer.Display.height() >> 1) + y2 + 1 - y;
							prev_y[x] = y;
							prev_h[x] = h;
							M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], WHITE);
						}
					}
				}

				M5Cardputer.Display.display();
				M5Cardputer.Display.fillCircle(70, 15, 8, RED);
				M5Cardputer.Display.drawString("REC", 120, 3);
			}

			if (saveWAVToSD(rec_data, record_size)) {
				printf("WAV file saved successfully.\n");
			} else {
				printf("Failed to save WAV file.\n");
			}
			M5Cardputer.Display.clear();
		}

		updateDisplay(wavFiles, selectedFileIndex);
	}

	scanAndDisplayWAVFiles();
}

void updateDisplay(const std::vector<String>& files, uint8_t selectedIndex)
{
	if (files.empty()) {
		printf("No WAV files found on SD card.\n");
		M5Cardputer.Display.fillScreen(BLACK);
		M5Cardputer.Display.setTextColor(RED);
		int xPos = M5Cardputer.Display.width() / 2;
		int yPos = M5Cardputer.Display.height() / 2 - 20;
		M5Cardputer.Display.drawString("No WAV files found", xPos, yPos);
		return;
	}

	const uint8_t maxVisibleFiles = 5;
	uint8_t startIndex            = 0;
	if (selectedIndex >= maxVisibleFiles) {
		startIndex = selectedIndex - (maxVisibleFiles - 1);
	}

	M5Cardputer.Display.fillScreen(BLACK);
	for (size_t i = startIndex; i < startIndex + maxVisibleFiles && i < files.size(); i++) {
		uint16_t color = (i == selectedIndex) ? YELLOW : WHITE;
		M5Cardputer.Display.setTextColor(color);
		M5Cardputer.Display.drawString(files[i], M5Cardputer.Display.width() / 2,
									   3 + (i - startIndex) * 25);
	}
}

void scanAndDisplayWAVFiles()
{
	static std::vector<String> previousWavFiles;

	File dir = SD.open("/");
	if (!dir) {
		printf("Failed to open directory.\n");
		return;
	}

	wavFiles.clear();
	while (File entry = dir.openNextFile()) {
		if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) {
			wavFiles.push_back(String(entry.name()));
		}
		entry.close();
	}
	dir.close();

	if (!wavFiles.empty() && selectedFileIndex >= wavFiles.size()) {
		selectedFileIndex = wavFiles.size() - 1;
	}

	if (wavFiles != previousWavFiles) {
		previousWavFiles = wavFiles;
		updateDisplay(wavFiles, selectedFileIndex);
	}

	if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
		if (wavFiles.empty()) {
			return;
		}

		Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
		char key                          = 0;
		for (auto c : status.word) {
			key = c;
		}

		if (key == ';') {
			selectedFileIndex = (selectedFileIndex == 0) ? wavFiles.size() - 1 : selectedFileIndex - 1;
			updateDisplay(wavFiles, selectedFileIndex);
		}
		if (key == '.') {
			selectedFileIndex = (selectedFileIndex + 1) % wavFiles.size();
			updateDisplay(wavFiles, selectedFileIndex);
		}

		if (status.del) {
			String filePath = "/" + wavFiles[selectedFileIndex];
			if (SD.remove(filePath.c_str())) {
				printf("Deleted file: %s\n", filePath.c_str());
				wavFiles.erase(wavFiles.begin() + selectedFileIndex);
				if (selectedFileIndex >= wavFiles.size() && !wavFiles.empty()) {
					selectedFileIndex--;
				}
				updateDisplay(wavFiles, selectedFileIndex);
			} else {
				printf("Failed to delete file: %s\n", filePath.c_str());
			}
		}

		if (status.enter && !wavFiles.empty()) {
			playSelectedWAVFile(wavFiles[selectedFileIndex]);
		}
	}
}

bool playSelectedWAVFile(const String& fileName)
{
	String filePath = fileName.startsWith("/") ? fileName : "/" + fileName;
	printf("Playing WAV file: %s\n", filePath.c_str());

	File file = SD.open(filePath.c_str());
	if (!file) {
		printf("Failed to open WAV file: %s\n", filePath.c_str());
		return false;
	}

	// Skip WAV header (usually 44 bytes).
	file.seek(44);

	const size_t maxRead = record_size * sizeof(int16_t);
	size_t bytesRead     = file.read(reinterpret_cast<uint8_t*>(rec_data), maxRead);
	file.close();

	if (bytesRead == 0) {
		printf("Failed to read WAV file data.\n");
		return false;
	}
	if (bytesRead < maxRead) {
		memset(reinterpret_cast<uint8_t*>(rec_data) + bytesRead, 0, maxRead - bytesRead);
	}

	playWAV();
	printf("Playback finished.\n");
	return true;
}

bool playWAVFileFromSD(void)
{
	File dir       = SD.open("/");
	String wavFile = "";

	while (File entry = dir.openNextFile()) {
		if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) {
			wavFile = "/" + String(entry.name());
			entry.close();
			break;
		}
		entry.close();
	}
	dir.close();

	if (wavFile == "") {
		printf("No WAV files found on SD card.\n");
		return false;
	}

	printf("Playing WAV file: %s\n", wavFile.c_str());
	File file = SD.open(wavFile.c_str());
	if (!file) {
		printf("Failed to open WAV file: %s\n", wavFile.c_str());
		return false;
	}

	file.seek(44);
	const size_t maxRead = record_size * sizeof(int16_t);
	size_t bytesRead     = file.read(reinterpret_cast<uint8_t*>(rec_data), maxRead);
	file.close();

	if (bytesRead == 0) {
		printf("Failed to read WAV file data.\n");
		return false;
	}
	if (bytesRead < maxRead) {
		memset(reinterpret_cast<uint8_t*>(rec_data) + bytesRead, 0, maxRead - bytesRead);
	}

	playWAV();
	printf("Playback finished.\n");
	return true;
}

void playWAV(void)
{
	M5Cardputer.Display.clear();
	M5Cardputer.Mic.end();
	M5Cardputer.Speaker.begin();
	M5Cardputer.Display.fillTriangle(70 - 8, 15 - 8, 70 - 8, 15 + 8, 70 + 8, 15, 0x1c9f);
	M5Cardputer.Display.drawString("PLAY", 120, 3);

	static constexpr int shift = 6;
	for (uint16_t i = 0; i < record_number; i++) {
		auto data = &rec_data[i * record_length];
		M5Cardputer.Speaker.playRaw(&rec_data[i * record_length], record_length, record_samplerate);

		do {
			delay(1);
			M5Cardputer.update();
		} while (M5Cardputer.Speaker.isPlaying());

		if (i >= 2) {
			data      = &rec_data[(i - 2) * record_length];
			int32_t w = M5Cardputer.Display.width();
			if (w > static_cast<int32_t>(record_length - 1)) {
				w = record_length - 1;
			}

			for (int32_t x = 0; x < w; ++x) {
				M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], TFT_BLACK);
				int32_t y1 = (data[x] >> shift);
				int32_t y2 = (data[x + 1] >> shift);
				if (y1 > y2) {
					int32_t tmp = y1;
					y1          = y2;
					y2          = tmp;
				}
				int32_t y = (M5Cardputer.Display.height() >> 1) + y1;
				int32_t h = (M5Cardputer.Display.height() >> 1) + y2 + 1 - y;
				prev_y[x] = y;
				prev_h[x] = h;
				M5Cardputer.Display.writeFastVLine(x, prev_y[x], prev_h[x], WHITE);
			}
		}

		M5Cardputer.Display.fillTriangle(70 - 8, 15 - 8, 70 - 8, 15 + 8, 70 + 8, 15, 0x1c9f);
		M5Cardputer.Display.drawString("PLAY", 120, 3);
	}

	M5Cardputer.Speaker.end();
#ifndef pcm1808_f
  M5Cardputer.Mic.begin();
#endif
  M5Cardputer.Display.clear();
	updateDisplay(wavFiles, selectedFileIndex);
}

bool saveWAVToSD(int16_t* data, size_t dataSize)
{
	char filename[32];
	snprintf(filename, sizeof(filename), "/recorded%lu.wav", file_counter++);

	File file = SD.open(filename, FILE_WRITE);
	if (!file) {
		printf("Failed to open file for writing.\n");
		return false;
	}

	WAVHeader header;
	header.fileSize = 36 + dataSize * sizeof(int16_t);
	header.dataSize = dataSize * sizeof(int16_t);

	file.write(reinterpret_cast<uint8_t*>(&header), sizeof(WAVHeader));
	file.write(reinterpret_cast<uint8_t*>(data), dataSize * sizeof(int16_t));
	file.close();

	return true;
}
