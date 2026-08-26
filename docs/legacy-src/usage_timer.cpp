#include "usage_timer.h"

extern void speakTTS(const String& text);  // 다른 모듈에 구현되어 있다고 가정

namespace {
  bool active = false;
  bool paused = false;
  unsigned long endMillis = 0;
  unsigned long remainingWhenPaused = 0;
}

void armUsageTimer(int minutes) {
  active = true;
  paused = false;
  endMillis = millis() + (unsigned long)minutes * 60UL * 1000UL;
  Serial.printf("[Timer] %d분 타이머 시작\n", minutes);
}

void pauseUsageTimer() {
  if (active && !paused) {
    paused = true;
    remainingWhenPaused = endMillis - millis();
    Serial.println("[Timer] 일시정지 (공부 프로그램 사용 중)");
  }
}

void resumeUsageTimer() {
  if (active && paused) {
    paused = false;
    endMillis = millis() + remainingWhenPaused;
    Serial.println("[Timer] 재개");
  }
}

void cancelUsageTimer() {
  active = false;
  paused = false;
  Serial.println("[Timer] 취소");
}

void checkUsageTimer() {
  if (!active || paused) return;

  if ((long)(millis() - endMillis) >= 0) {
    active = false;
    speakTTS("이제 그만해야 할 시간이에요. 잠깐 쉬어갈까요?");
    Serial.println("[Timer] 종료, 안내 완료");
  }
}
