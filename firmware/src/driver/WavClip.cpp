#include "WavClip.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <cstring>
#include "Volume.h"

extern "C" {
extern const uint8_t stagewin_wav_start[];
extern const uint8_t stagewin_wav_end[];
extern const uint8_t shutterclick_wav_start[];
extern const uint8_t shutterclick_wav_end[];
}

namespace {
uint16_t le16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void* audioAlloc(size_t bytes) {
#if CONFIG_SPIRAM
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool parseWav(const uint8_t* data, size_t len, const uint8_t*& pcm, uint32_t& bytes,
              uint32_t& rate, uint16_t& channels, uint16_t& bits) {
  if (data == nullptr || len < 44) return false;
  if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return false;
  size_t pos = 12;
  bool haveFmt = false;
  bool haveData = false;
  while (pos + 8 <= len) {
    const uint8_t* id = data + pos;
    uint32_t sz = le32(data + pos + 4);
    pos += 8;
    if (pos + sz > len) return false;
    if (!memcmp(id, "fmt ", 4) && sz >= 16) {
      uint16_t format = le16(data + pos);
      channels = le16(data + pos + 2);
      rate = le32(data + pos + 4);
      bits = le16(data + pos + 14);
      if (format != 1 && format != 0xFFFE) return false;
      haveFmt = true;
    } else if (!memcmp(id, "data", 4)) {
      pcm = data + pos;
      bytes = sz;
      haveData = true;
      break;
    }
    pos += sz + (sz & 1);
  }
  return haveFmt && haveData && bits == 16 && channels >= 1 && channels <= 2 && rate > 0;
}

bool playBlob(const uint8_t* start, const uint8_t* end, const char* tag, bool restoreMic) {
  if (start == nullptr || end == nullptr || end <= start) {
    Serial.printf("[wav] empty clip %s\n", tag ? tag : "?");
    return false;
  }
  const uint8_t* pcm = nullptr;
  uint32_t bytes = 0, rate = 0;
  uint16_t ch = 0, bits = 0;
  if (!parseWav(start, (size_t)(end - start), pcm, bytes, rate, ch, bits)) {
    Serial.printf("[wav] bad header %s\n", tag ? tag : "?");
    return false;
  }
  int16_t* copy = (int16_t*)audioAlloc(bytes);
  if (!copy) {
    Serial.printf("[wav] alloc failed %s (%u bytes)\n", tag ? tag : "?", (unsigned)bytes);
    return false;
  }
  memcpy(copy, pcm, bytes);
  size_t samples = bytes / 2;
  if (ch == 2) {
    size_t frames = samples / 2;
    for (size_t i = 0; i < frames; ++i) {
      int32_t v = (int32_t)copy[2 * i] + copy[2 * i + 1];
      copy[i] = (int16_t)(v / 2);
    }
    samples = frames;
  }

  while (M5.Speaker.isPlaying()) delay(2);
  if (M5.Mic.isEnabled()) M5.Mic.end();
  M5.Speaker.begin();
  int vol = volume_get();
  if (vol < 8) vol = 120;
  M5.Speaker.setVolume((uint8_t)vol);
  Serial.printf("[wav] play %s samples=%u rate=%u\n", tag ? tag : "?", (unsigned)samples, (unsigned)rate);
  bool ok = M5.Speaker.playRaw(copy, samples, rate, false, 1, 0);
  if (ok) {
    while (M5.Speaker.isPlaying()) {
      M5.update();
      delay(2);
    }
  }
  free(copy);
  M5.Speaker.end();
  delay(30);
  if (restoreMic) M5.Mic.begin();
  return ok;
}
}  // namespace

bool wav_clip_play_stagewin(bool restoreMic) {
  return playBlob(stagewin_wav_start, stagewin_wav_end, "stagewin", restoreMic);
}

bool wav_clip_play_shutter(bool restoreMic) {
  return playBlob(shutterclick_wav_start, shutterclick_wav_end, "shutterclick", restoreMic);
}
