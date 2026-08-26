#include "usage_timer.h"
#include "Robot.h"

extern Robot* robot;

namespace {
  bool active = false;
  bool paused = false;
  unsigned long endMillis = 0;
  unsigned long remainingWhenPaused = 0;
}

void armUsageTimer(int minutes) {
  if (minutes <= 0) minutes = 30;
  active = true;
  paused = false;
  endMillis = millis() + (unsigned long)minutes * 60UL * 1000UL;
  Serial.printf("[usage-timer] start %d min\n", minutes);
}

void pauseUsageTimer() {
  if (active && !paused) {
    paused = true;
    remainingWhenPaused = endMillis - millis();
    Serial.println("[usage-timer] paused");
  }
}

void resumeUsageTimer() {
  if (active && paused) {
    paused = false;
    endMillis = millis() + remainingWhenPaused;
    Serial.println("[usage-timer] resumed");
  }
}

void cancelUsageTimer() {
  active = false;
  paused = false;
  Serial.println("[usage-timer] canceled");
}

bool isUsageTimerActive() {
  return active;
}

void checkUsageTimer() {
  if (!active || paused) return;
  if ((long)(millis() - endMillis) >= 0) {
    active = false;
    Serial.println("[usage-timer] expired");
    if (robot) {
      robot->speech("이제 그만해야 할 시간이에요. 잠깐 쉬어갈까요?");
    }
  }
}
