#pragma once
#include <Arduino.h>

// 일반 사용(챗봇/음악 등) 30분 제한 타이머. 공부 프로그램 사용 중에는 pause() 호출.
void armUsageTimer(int minutes);
void pauseUsageTimer();
void resumeUsageTimer();
void cancelUsageTimer();

// main loop()에서 매 루프 호출: 시간이 다 되면 TTS 안내 후 자동으로 타이머를 정지한다.
void checkUsageTimer();
