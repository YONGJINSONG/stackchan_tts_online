#include "CameraAction.h"

#include <Avatar.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "share/SdBus.h"

#if defined(ENABLE_CAMERA)
#include "driver/Camera.h"
#endif

using namespace m5avatar;
extern Avatar avatar;

namespace {
enum class ActionState : uint8_t { Idle, Pending, Running, Done };
SemaphoreHandle_t s_mux = nullptr;
ActionState s_state = ActionState::Idle;
String s_result;
bool s_abandonWhenDone = false;

void ensure_mux() {
  if (s_mux == nullptr) s_mux = xSemaphoreCreateMutex();
}

void lock_action() {
  ensure_mux();
  xSemaphoreTake(s_mux, portMAX_DELAY);
}

void unlock_action() {
  xSemaphoreGive(s_mux);
}

#if defined(ENABLE_CAMERA)
bool preview_saved_photo(String& path) {
  sd_bus_lock();
  bool ok = avatar.updateSubWindowJpg(path);
  sd_bus_unlock();
  if (ok) {
    avatar.set_isSubWindowEnable(true);
    Serial.printf("[photo-preview] subwindow ok: %s\n", path.c_str());
  } else {
    Serial.printf("[photo-preview] load failed: %s\n", path.c_str());
  }
  return ok;
}

String run_save_action() {
  String path;
  if (!camera_capture_save_sd(path)) {
    return "{\"error\":\"사진 촬영에 실패했어요.\"}";
  }
  bool shown = preview_saved_photo(path);
  String result = String("{\"result\":\"사진을 저장했어요.\",\"path\":\"") + path + "\"";
  result += shown ? ",\"preview\":true}" : ",\"preview\":false}";
  return result;
}
#endif
}  // namespace

bool camera_action_request_save() {
#if !defined(ENABLE_CAMERA)
  return false;
#else
  lock_action();
  if (s_state != ActionState::Idle) {
    unlock_action();
    Serial.println("[camera-action] rejected: busy");
    return false;
  }
  s_result = "";
  s_abandonWhenDone = false;
  s_state = ActionState::Pending;
  unlock_action();
  Serial.println("[camera-action] save queued");
  return true;
#endif
}

void camera_action_process_pending() {
#if defined(ENABLE_CAMERA)
  lock_action();
  if (s_state != ActionState::Pending) {
    unlock_action();
    return;
  }
  s_state = ActionState::Running;
  unlock_action();

  Serial.println("[camera-action] capture started on loop task");
  String result = run_save_action();

  lock_action();
  if (s_abandonWhenDone) {
    s_result = "";
    s_state = ActionState::Idle;
    s_abandonWhenDone = false;
    Serial.println("[camera-action] result abandoned");
  } else {
    s_result = result;
    s_state = ActionState::Done;
    Serial.println("[camera-action] result ready");
  }
  unlock_action();
#endif
}

bool camera_action_take_result(String& result) {
#if !defined(ENABLE_CAMERA)
  (void)result;
  return false;
#else
  lock_action();
  if (s_state != ActionState::Done) {
    unlock_action();
    return false;
  }
  result = s_result;
  s_result = "";
  s_state = ActionState::Idle;
  unlock_action();
  return true;
#endif
}

void camera_action_abandon() {
#if defined(ENABLE_CAMERA)
  lock_action();
  if (s_state == ActionState::Running) {
    s_abandonWhenDone = true;
  } else {
    s_result = "";
    s_state = ActionState::Idle;
    s_abandonWhenDone = false;
  }
  unlock_action();
#endif
}
