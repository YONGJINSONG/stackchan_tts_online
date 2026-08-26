#pragma once
#include <Arduino.h>

// General-use time limit (chat / music). Pause while a study program is running.
void armUsageTimer(int minutes);
void pauseUsageTimer();
void resumeUsageTimer();
void cancelUsageTimer();
bool isUsageTimerActive();

// Call from loop(): on expiry, speak a Korean wrap-up and stop.
void checkUsageTimer();
