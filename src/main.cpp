#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SD.h>
#include "AudioTools.h"

// 状態管理
enum State {
  RECORDING,
  PLAYING,
  IDLE
};
State currentState = RECORDING;

const char* audioFilePath = "/recording.wav";
File audioFile;
SPIClass SPI2;

// --- AudioToolsのオブジェクト ---
// 録音用
WAVEncoder wavEncoder;
I2SStream i2sIn;
EncodedAudioStream audioStreamEncoder(&audioFile, &wavEncoder);
StreamCopy copier(audioStreamEncoder, i2sIn);

// 再生用
WAVDecoder wavDecoder;
I2SStream i2sOut;
EncodedAudioStream audioStreamDecoder(&i2sOut, &wavDecoder);
StreamCopy playCopier(audioStreamDecoder, audioFile);


// プロトタイプ宣言
void startRecording();
void stopRecordingAndPlay();
void startPlayback();

void setup() {
  // M5Cardputerの初期化
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.println("M5Cardputer Audio Recorder");

  // --- SDカードの初期化 ---
  M5Cardputer.Display.println("Initializing SD card...");
  // SCK, MISO, MOSI, CS
  SPI2.begin(40, 39, 14, 12);
  if (!SD.begin(12, SPI2)) {
    M5Cardputer.Display.println("SD card mount failed!");
    while (true) { delay(1000); }
  }
  M5Cardputer.Display.println("SD card mounted.");
  // 既存のファイルを削除
  if (SD.exists(audioFilePath)) {
    SD.remove(audioFilePath);
  }

  // --- I2Sとマイク/スピーカーの初期化 ---
  // M5Cardputer.Mic.begin();
  M5Cardputer.Mic.end(); // デフォルト無効化
  // delay(100);
  // M5Cardputer.Speaker.begin();
  // M5Cardputer.Speaker.setVolume(255);
  M5Cardputer.Speaker.end(); // デフォルト無効化
  delay(100);
  // すぐに録音を開始
  startRecording();
}

void loop() {
  M5Cardputer.update();

  switch (currentState) {
    case RECORDING:
      // I2Sから読み取ったデータをSDカードのファイルにコピーする
      copier.copy();
      if (M5Cardputer.BtnA.wasPressed()) {
        stopRecordingAndPlay();
      }
      break;

    case PLAYING:
      // 再生が終わったかチェック
      if (!playCopier.copy()) {
        M5Cardputer.Display.println("Playback finished. Press BtnA to play again.");
        playCopier.end();
        i2sOut.end();
        audioFile.close();
        currentState = IDLE;
      }
      break;

    case IDLE:
      // 再生終了後、BtnAで再度再生
      if (M5Cardputer.BtnA.wasPressed()) {
        startPlayback();
      }
      break;
  }
}

void startRecording() {
  M5Cardputer.Display.println("Recording... Press BtnA to stop.");
  
  // スピーカーを無効化し、マイクを有効化
  M5Cardputer.Speaker.end();
  M5Cardputer.Speaker.setVolume(255);
//  delay(100); // 確実にスピーカーが停止するように少し待つ
  M5Cardputer.Mic.begin();
  delay(1); 

  // 書き込み用にファイルを開く
  audioFile = SD.open(audioFilePath, FILE_WRITE);
  if (!audioFile) {
    M5Cardputer.Display.println("Failed to open file for writing");
    while (true) { delay(1000); }
  }

  // 録音用のI2S設定
  auto cfg_in = i2sIn.defaultConfig(RX_MODE);
  cfg_in.pin_bck = 41;
  cfg_in.pin_ws = 43;
  cfg_in.pin_data = -1; // 使用しない
  cfg_in.pin_data_rx = 46;
  cfg_in.sample_rate = 16000;
  cfg_in.bits_per_sample = 16;
  cfg_in.channels = 1;
  i2sIn.begin(cfg_in);

  // エンコーダーの設定
  auto wav_cfg = wavEncoder.defaultConfig();
  wav_cfg.sample_rate = 16000;
  wav_cfg.bits_per_sample = 16;
  wav_cfg.channels = 1;
  audioStreamEncoder.begin(wav_cfg);
  
  copier.begin();
  currentState = RECORDING;
}

void stopRecordingAndPlay() {
  M5Cardputer.Display.println("Recording stopped.");
  
  // 録音を終了
  copier.end();
  i2sIn.end();
  audioFile.close();

  // テストのためここで停止
  while(true) { delay(1000); }

  // 再生を開始
  startPlayback();
}

void startPlayback() {
  M5Cardputer.Display.println("Playing...");

  // マイクを無効化し、スピーカーを有効化
  M5Cardputer.Mic.end();
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(255);
  delay(1); 

  // 読み込み用にファイルを開く
  audioFile = SD.open(audioFilePath, FILE_READ);
  if (!audioFile) {
    M5Cardputer.Display.println("Failed to open file for reading");
    return;
  }

  // 再生用のI2S設定
  auto cfg_out = i2sOut.defaultConfig(TX_MODE);
  cfg_out.pin_bck = 41;
  cfg_out.pin_ws = 43;
  cfg_out.pin_data = 42;
  cfg_out.pin_data_rx = -1; // 使用しない
  cfg_out.sample_rate = 16000;
  cfg_out.bits_per_sample = 16;
  cfg_out.channels = 1;
  i2sOut.begin(cfg_out);

  // デコーダーと再生用StreamCopyを開始
  audioStreamDecoder.begin();
  playCopier.begin();
  
  currentState = PLAYING;
}
