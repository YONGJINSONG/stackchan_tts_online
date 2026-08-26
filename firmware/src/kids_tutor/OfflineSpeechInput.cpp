#include "OfflineSpeechInput.h"
#include "TutorConfig.h"

#if TUTOR_ENABLE_ESPSR
extern "C" bool stackchan_espsr_begin(void);
extern "C" bool stackchan_espsr_listen(char* out, size_t outSize, uint32_t timeoutMs);
#endif

bool OfflineSpeechInput::begin(fs::FS& fs) {
  (void)fs;
#if TUTOR_ENABLE_ESPSR
  _available = stackchan_espsr_begin();
#else
  _available = false;
#endif
  return _available;
}

bool OfflineSpeechInput::listen(String& transcript, uint32_t timeoutMs) {
  transcript = "";
#if TUTOR_ENABLE_ESPSR
  if (!_available) return false;
  char buf[96] = {0};
  if (!stackchan_espsr_listen(buf, sizeof(buf), timeoutMs)) return false;
  transcript = String(buf); transcript.trim();
  return transcript.length() > 0;
#else
  (void)timeoutMs;
  return false;
#endif
}
