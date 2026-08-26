#pragma once
#include <Arduino.h>
#include <FS.h>

// Optional CoreS3 ESP-SR bridge.
// The normal v4 build does NOT require ESP-SR and always works with buttons.
// If TUTOR_ENABLE_ESPSR=1, provide the two hook functions documented in
// optional_esp_sr/README_KO.md from an ESP-IDF/ESP-SR component.
class OfflineSpeechInput {
public:
  bool begin(fs::FS& fs);
  bool available() const { return _available; }
  bool listen(String& transcript, uint32_t timeoutMs = 5000);
private:
  bool _available = false;
};
