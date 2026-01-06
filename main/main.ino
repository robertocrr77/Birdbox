#include <SPI.h>
#include <SD.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/CoreAudio/VolumeStream.h"
#include "AudioTools/CoreAudio/AudioLogger.h"

constexpr int PIN_SD_CS   = 10;  // <-- set to your SD CS
constexpr int PIN_SD_SCK  = 6;   // <-- set to your SD SCK
constexpr int PIN_SD_MISO = 5;   // <-- set to your SD MISO
constexpr int PIN_SD_MOSI = 7;   // <-- set to your SD MOSI

constexpr int PIN_I2S_BCLK = 3;  // <-- set to your I2S BCLK
constexpr int PIN_I2S_LRCK = 4;  // <-- set to your I2S LRCLK
constexpr int PIN_I2S_DOUT = 2;  // <-- set to your I2S DIN

constexpr int PIN_TRIGGER = 9;   // <-- set to your trigger input (active-low, pull-up expected)

constexpr char kFiles[][32] = {
  "/music/file1.mp3",
  "/music/file2.mp3",
  "/music/file3.mp3",
  "/music/file4.mp3",
  "/music/file5.mp3"
};
constexpr size_t kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);

int playOrder[kFileCount];
size_t playIndex = 0;
bool sequenceActive = false;

SPIClass spi = SPI;
audio_tools::AudioLogger &logger = audio_tools::AudioToolsLogger;
I2SStream i2s;                                     // I2S output to DAC/amp
VolumeStream volume(i2s);                          // Volume-controlled sink
EncodedAudioStream decoder(&volume, new MP3DecoderHelix()); // MP3 decoder
StreamCopy copier;                                 // Moves data from SD to decoder
File audioFile;

bool startFile(size_t idx) {
  if (audioFile) {
    audioFile.close();
  }
  audioFile = SD.open(kFiles[idx]);
  if (!audioFile) {
    logger.print("File open failed");
    logger.println();
    return false;
  }
  copier.begin(decoder, audioFile);
  return true;
}

void shufflePlayOrder() {
  for (size_t i = 0; i < kFileCount; ++i) {
    playOrder[i] = static_cast<int>(i);
  }
  for (size_t i = kFileCount - 1; i > 0; --i) {
    size_t j = static_cast<size_t>(random(i + 1));
    int tmp = playOrder[i];
    playOrder[i] = playOrder[j];
    playOrder[j] = tmp;
  }
}

void setup() {
  Serial.begin(115200);
  logger.begin(Serial, AudioToolsLogLevel::Info);

  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  randomSeed(static_cast<uint32_t>(micros()));

  // Bring up SD over SPI
  spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, spi, 8000000U)) {       // 8 MHz; raise if your wiring is short/clean
    logger.print("SD init failed");
    logger.println();
    while (true) delay(1000);
  }

  // Configure I2S for playback
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  cfg.pin_bck = PIN_I2S_BCLK;
  cfg.pin_ws  = PIN_I2S_LRCK;
  cfg.pin_data = PIN_I2S_DOUT;
  cfg.i2s_format = I2S_STD_FORMAT;
  i2s.begin(cfg);

  volume.setVolume(0.8f); // 0.0..2.0, 1.0 = unity

  // Start decoder; playback begins after trigger
  decoder.begin();
}

void loop() {
  static int lastTrigger = HIGH;
  const int triggerState = digitalRead(PIN_TRIGGER);

  if (!sequenceActive && lastTrigger == HIGH && triggerState == LOW) {
    shufflePlayOrder();
    playIndex = 0;
    sequenceActive = startFile(playOrder[playIndex]);
  }
  lastTrigger = triggerState;

  if (sequenceActive) {
    if (!copier.copy()) {
      audioFile.close();
      ++playIndex;
      if (playIndex >= kFileCount) {
        sequenceActive = false;
      } else {
        sequenceActive = startFile(playOrder[playIndex]);
      }
    }
  }
}