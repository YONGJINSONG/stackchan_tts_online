#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "Gesture.h"
#include "Robot.h"
#include "CameraVision.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;

extern volatile uint32_t g_servoManualUntil;   // main.cpp — 웹 조이스틱 수동 머리 제어 중

extern Avatar avatar;

volatile uint32_t gesture_suppress_until = 0;
volatile bool g_gesture_playing = false;
static volatile bool g_gesture_motion_hold = false;
static QueueHandle_t gestureQueue = NULL;

enum class GestureCommandType : uint8_t {
  Expression,
  IdleLook,
  Dance,
};

struct GestureCommand {
  GestureCommandType type;
  Expression expression;
};

struct GestureStep {
  int degX;
  int degY;
  uint32_t ms;
};

static void play_sequence(const GestureStep* steps, int n, uint32_t tail_pad_ms) {
  if(robot == nullptr || robot->servo == nullptr){
    Serial.println("[gesture] ABORT: robot/servo null");
    return;
  }
  if (camera_is_busy()) {
    Serial.println("[gesture] skip (camera owns servo GPIO/I2C)");
    return;
  }
  if (g_gesture_motion_hold) {
    Serial.println("[gesture] skip (motion held)");
    return;
  }
  extern volatile bool espnow_remote_servo_override;
  if (espnow_remote_servo_override) {
    Serial.println("[gesture] skip (espnow remote owns servo)");
    return;
  }
#if defined(REALTIME_API)
  // Servo motion + Serial spam during listen competes with audio_append TLS writes.
  if (robot->llm && ((RealtimeLLMBase*)robot->llm)->isRealtimeRecording()) {
    Serial.println("[gesture] skip (listening)");
    return;
  }
#endif
  if(millis() < g_servoManualUntil){   // 웹 조이스틱 수동 제어 중엔 제스처로 머리 안 움직임
    Serial.println("[gesture] skip (manual head control)");
    return;
  }

  uint32_t total = 0;
  for(int i = 0; i < n; i++) total += steps[i].ms;
  gesture_suppress_until = millis() + total + tail_pad_ms + 2000;
  Serial.printf("[gesture] sequence n=%d total=%ums suppress=%ums\n", n, total, total + tail_pad_ms + 2000);

  g_gesture_playing = true;
  for(int i = 0; i < n; i++){
    if (camera_is_busy() || g_gesture_motion_hold) {
      Serial.println("[gesture] abort remaining steps (motion held/camera busy)");
      break;
    }
    Serial.printf("[gesture]   step %d: x=%d y=%d ms=%u\n", i, steps[i].degX, steps[i].degY, steps[i].ms);
    robot->servo->moveTo(steps[i].degX, steps[i].degY, steps[i].ms);
    delay(steps[i].ms);
  }
  g_gesture_playing = false;
}

static void gesture_task(void* arg) {
  GestureCommand command;
  for(;;){
    if(xQueueReceive(gestureQueue, &command, portMAX_DELAY) != pdTRUE) continue;

    if (command.type == GestureCommandType::Dance) {
#ifdef USE_SERVO
      Expression previous = avatar.getExpression();
      avatar.setExpression(Expression::Happy);
      const GestureStep dance[] = {
        {-24, -10, 350}, {24, -10, 350},
        {-18,  12, 350}, {18,  12, 350},
        {-28,  -5, 300}, {28,  -5, 300},
        {  0, -18, 300}, { 0,   8, 300},
        {-20,   0, 300}, {20,   0, 300},
        {  0,   0, 400},
      };
      Serial.println("[gesture] dance started");
      play_sequence(dance, sizeof(dance) / sizeof(dance[0]), 0);
      if (avatar.getExpression() == Expression::Happy) {
        avatar.setExpression(previous);
      }
      Serial.println("[gesture] dance finished");
#endif
      continue;
    }
    if (command.type == GestureCommandType::IdleLook) {
#ifdef USE_SERVO
      // Idle에서는 의미 있는 끄덕임(Y)을 사용하지 않는다.
      // IdleMotion already schedules this sparingly (150--210 seconds), so
      // every accepted idle request performs one small, predictable glance.
      int roll = random(100);

      int x;

      if (roll < 75) {
        // Usually a gentle left/right glance of 3--6 degrees.
        int mag = random(3, 7);
        x = random(2) ? mag : -mag;
      } else {
        // Occasionally only a tiny micro movement.
        int mag = random(1, 4);
        x = random(2) ? mag : -mag;
      }

      uint32_t moveOut = random(500, 751);
      uint32_t hold = random(600, 1201);
      uint32_t moveBack = random(500, 751);

      // Y=0 고정.
      // 중간에 같은 위치를 다시 지정해 잠시 바라보다가 중앙으로 복귀.
      const GestureStep idle[] = {
        {x, 0, moveOut},
        {x, 0, hold},
        {0, 0, moveBack},
      };

      Serial.printf(
        "[gesture] idle look x=%d hold=%ums\n",
        x,
        (unsigned)hold
      );

      play_sequence(idle, 3, 500);
#endif
      continue;
    }

    Expression e = command.expression;

    Serial.printf("[gesture] dequeued expression=%d\n", (int)e);
    switch(e){
      case Expression::Happy: {
        // 긍정/기쁨 = 상하로 빠르게 끄덕끄덕 (degY 음수 = 고개 숙임, 0 = 중앙). 좌우 흔들기에서 변경.
        // 범위 살짝 축소(-22→-15) + 속도 향상(160/140→100/85ms).
        const GestureStep s[] = {{0, -15, 100}, {0, 0, 85}, {0, -15, 100}, {0, 0, 85}};
        play_sequence(s, 4, 0);
        break;
      }
      case Expression::Sad: {
        const GestureStep s[] = {{0, -40, 600}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Angry: {
        const GestureStep s[] = {{30, 0, 150}, {-30, 0, 150}, {0, 0, 200}};
        play_sequence(s, 3, 0);
        break;
      }
      case Expression::Doubt: {
        const GestureStep s[] = {{15, 0, 400}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Sleepy: {
        const GestureStep s[] = {{0, -30, 1500}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Neutral:
      default: {
        const GestureStep s[] = {{0, 0, 800}};
        play_sequence(s, 1, 0);
        break;
      }
    }
  }
}

void gesture_init() {
  if(gestureQueue != NULL) return;
  gestureQueue = xQueueCreate(1, sizeof(GestureCommand));
  xTaskCreatePinnedToCore(gesture_task, "gesture", 4096, NULL, 1, NULL, APP_CPU_NUM);
  Serial.println("[gesture] task started");
}

void gesture_play(Expression e) {
  if(gestureQueue == NULL){
    Serial.println("[gesture] play called but queue is NULL");
    return;
  }
  if (g_gesture_motion_hold || camera_is_busy()) {
    return;
  }
#if defined(REALTIME_API)
  if (robot && robot->llm && ((RealtimeLLMBase*)robot->llm)->isRealtimeRecording()) {
    // Don't even enqueue — queue overwrite + Serial during listen competes with TLS.
    return;
  }
#endif
  extern volatile bool espnow_remote_servo_override;
  if (espnow_remote_servo_override) {
    return;  // remote owns the head
  }
  Serial.printf("[gesture] play enqueue expression=%d\n", (int)e);
  GestureCommand command{GestureCommandType::Expression, e};
  xQueueOverwrite(gestureQueue, &command);
}

void gesture_idle_look() {
  if (gestureQueue == NULL) return;

  if (g_gesture_motion_hold || camera_is_busy()) {
    return;
  }

#if defined(REALTIME_API)
  if (robot && robot->llm &&
      ((RealtimeLLMBase*)robot->llm)->isRealtimeRecording()) {
    return;
  }
#endif

  extern volatile bool espnow_remote_servo_override;
  if (espnow_remote_servo_override) {
    return;
  }

  if (millis() < g_servoManualUntil) {
    return;
  }

  GestureCommand command{
    GestureCommandType::IdleLook,
    Expression::Neutral
  };

  // Idle motion is low priority. Never replace a queued expression or dance.
  xQueueSend(gestureQueue, &command, 0);
}

bool gesture_dance() {
#ifndef USE_SERVO
  Serial.println("[gesture] dance unavailable (servo disabled)");
  return false;
#else
  if (gestureQueue == NULL || robot == nullptr || robot->servo == nullptr) {
    Serial.println("[gesture] dance unavailable (servo not ready)");
    return false;
  }
  if (millis() < g_servoManualUntil) {
    Serial.println("[gesture] dance skipped (manual head control)");
    return false;
  }
  if (camera_is_busy()) {
    Serial.println("[gesture] dance skipped (camera busy)");
    return false;
  }
  if (g_gesture_motion_hold) {
    Serial.println("[gesture] dance skipped (motion held)");
    return false;
  }
  gesture_suppress_until = millis() + 7000;
  GestureCommand command{GestureCommandType::Dance, Expression::Happy};
  xQueueOverwrite(gestureQueue, &command);
  Serial.println("[gesture] dance queued");
  return true;
#endif
}

void gesture_set_motion_hold(bool hold, bool center) {
  g_gesture_motion_hold = hold;

  if (hold && gestureQueue != NULL) {
    xQueueReset(gestureQueue);
  }

  if (hold) {
    // M5_SCS moves block the gesture task for their command duration. Wait
    // before centering so two tasks never write Serial2 concurrently.
    uint32_t waitStart = millis();
    while (g_gesture_playing && millis() - waitStart < 2200) {
      delay(10);
    }
  }

  if (center && robot != nullptr && robot->servo != nullptr) {
    robot->servo->moveTo(0, 0, 350);
  }

  Serial.printf("[gesture] motion hold=%d center=%d\n", (int)hold, (int)center);
}

bool gesture_motion_held() {
  return g_gesture_motion_hold;
}
