#include <Arduino.h>
#include "esp_sound.h"
#include "SD.h"
#include "SPI.h"

#define SD_CARD_CS_PIN 21 // Correct for Xiao ESP32S3

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000);

  if (!SD.begin(SD_CARD_CS_PIN)) {
    log_e("Cannot init SD");
    while(1);
  }
  Sound::init(44100, 22050, 20);
  Sound::startMic();
  Serial.println("Started listening");
}

int clipCount = 0;
Sound recording;
Clip* clip;
int loopCount = 0;

void loop() {
  
  if (clipCount < 7) {
    
    if (Sound::getMicClip(clip)) {
      log_i("Clip is %08x at loopCount %d", clip, loopCount);
      //log_i("Clip length before = %d", clip->length);
      //log_i("Sound length before = %f", recording.secs());
      // Sound::append() can throw an exception but you are not obliged to wrap it in a try/catch
      recording.append(std::move(*clip));
      //log_i("Clip length after = %d", clip->length);
      clipCount ++;
      //log_i("Sound length after = %f", recording.secs());
    }
  } else {
    try {
      Sound::stopMic();
      log_i("Recording is %3.1fs long", recording.secs());
      uint32_t start;
      recording.toFile(SD, "/temp.wav").save();
      log_i("Wrote file in %dms", millis() - start);
      recording.clear();
      SD.end();
      log_i("Loop count = %d", loopCount);
    }
    catch(std::exception const& ex) {
      log_e("Exception: %s", ex.what());
    }
    while(1);
  }
  loopCount++;
}

