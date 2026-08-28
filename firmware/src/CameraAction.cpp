#include "CameraAction.h"

#include <SD.h>
#include <M5Unified.h>
#include <Avatar.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "share/SdBus.h"
#include "llm/ChatGPT/FunctionCall.h"

#if defined(ENABLE_CAMERA)
#include "driver/Camera.h"
#endif

using namespace m5avatar;
extern Avatar avatar;

namespace m5avatar {
extern volatile bool g_avatar_render_pause;
}

namespace {
enum class ActionState : uint8_t {
  Idle, Countdown, Running, ReviewSave, Done
};
SemaphoreHandle_t s_mux = nullptr;
ActionState s_state = ActionState::Idle;
String s_result;
String s_tempPath;
String s_savedPath;
bool s_abandonWhenDone = false;
bool s_saved = false;
int s_countdownDigit = 0;
uint32_t s_countdownNextMs = 0;

constexpr int kBarH = 56;
const char* kPreviewPath = "/app/AiStackChanEx/photo/.preview.jpg";

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

void show_countdown_digit(int digit) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", digit);
  avatar.setSpeechText(buf);
  Serial.printf("[camera-action] countdown %d\n", digit);
}

int bar_top() {
  int h = M5.Display.height();
  return (h > kBarH) ? (h - kBarH) : 0;
}

void draw_buttons(const char* prompt, const char* left, const char* right) {
  const int w = M5.Display.width();
  const int h = M5.Display.height();
  const int top = bar_top();
  M5.Display.setFont(&fonts::efontKR_16);
  M5.Display.fillRect(0, top, w, h - top, TFT_BLACK);
  int btnTop = top + 4;
  if (prompt && prompt[0]) {
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int px = max(4, (w - M5.Display.textWidth(prompt)) / 2);
    M5.Display.drawString(prompt, px, top + 2);
    btnTop = top + 20;
  }
  const int btnH = h - btnTop - 4;
  const int half = w / 2;
  M5.Display.fillRoundRect(4, btnTop, half - 8, btnH, 6, TFT_DARKGREY);
  M5.Display.fillRoundRect(half + 4, btnTop, w - half - 8, btnH, 6, TFT_DARKGREY);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  int lx = 4 + max(2, ((half - 8) - M5.Display.textWidth(left)) / 2);
  int rx = half + 4 + max(2, ((w - half - 8) - M5.Display.textWidth(right)) / 2);
  int ty = btnTop + max(1, (btnH - 16) / 2);
  M5.Display.drawString(left, lx, ty);
  M5.Display.drawString(right, rx, ty);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

#if defined(ENABLE_CAMERA)
void draw_preview_jpeg(const String& path) {
  g_avatar_render_pause = true;
  avatar.set_isSubWindowEnable(false);
  avatar.setSpeechText("");
  const int w = M5.Display.width();
  const int imgH = bar_top();
  M5.Display.fillRect(0, 0, w, imgH, TFT_BLACK);

  sd_bus_lock();
  File f = SD.open(path.c_str(), FILE_READ);
  bool ok = false;
  if (f) {
    size_t n = f.size();
    if (n > 0 && n <= 300 * 1024) {
      uint8_t* buf = (uint8_t*)ps_malloc(n);
      if (buf) {
        size_t got = f.read(buf, n);
        f.close();
        sd_bus_unlock();
        if (got == n) {
          ok = M5.Display.drawJpg(buf, n, 0, 0, w, imgH);
        }
        free(buf);
      } else {
        f.close();
        sd_bus_unlock();
      }
    } else {
      f.close();
      sd_bus_unlock();
    }
  } else {
    sd_bus_unlock();
  }
  if (!ok) {
    M5.Display.setFont(&fonts::efontKR_16);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("미리보기 실패", 20, 40);
  }
}

void show_save_ui() {
  draw_preview_jpeg(s_tempPath);
  draw_buttons("", "저장하기", "저장안함");
}

void cleanup_temp() {
  if (s_tempPath.length()) {
    sd_bus_lock();
    SD.remove(s_tempPath.c_str());
    sd_bus_unlock();
    s_tempPath = "";
  }
}

void finish_idle_ui() {
  g_avatar_render_pause = false;
}

String build_result() {
  String msg = s_saved ? "사진을 저장했어요." : "사진을 저장하지 않았어요.";
  String json = "{\"result\":\"";
  json += msg;
  json += "\",\"saved\":";
  json += s_saved ? "true" : "false";
  if (s_saved && s_savedPath.length()) {
    json += ",\"path\":\"";
    json += s_savedPath;
    json += "\"";
  }
  json += "}";
  return json;
}

bool capture_preview() {
  s_tempPath = "";
  s_savedPath = "";
  s_saved = false;
  String path;
  if (!camera_capture_save_to(kPreviewPath, path)) return false;
  s_tempPath = path;
  return true;
}

bool commit_save() {
  String dest = String(APP_DATA_PATH) + "photo/" + String(millis()) + ".jpg";
  sd_bus_lock();
  bool ok = SD.rename(s_tempPath.c_str(), dest.c_str());
  if (!ok) {
    File src = SD.open(s_tempPath.c_str(), FILE_READ);
    File dst = SD.open(dest.c_str(), FILE_WRITE);
    if (src && dst) {
      uint8_t buf[1024];
      while (src.available()) {
        int n = src.read(buf, sizeof(buf));
        if (n > 0) dst.write(buf, n);
      }
      ok = true;
    }
    if (src) src.close();
    if (dst) dst.close();
    if (ok) SD.remove(s_tempPath.c_str());
  }
  sd_bus_unlock();
  if (!ok) return false;
  s_savedPath = dest;
  s_tempPath = dest;
  s_saved = true;
  return true;
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
  s_countdownDigit = 3;
  s_countdownNextMs = millis() + 1000;
  s_state = ActionState::Countdown;
  unlock_action();
  show_countdown_digit(3);
  Serial.println("[camera-action] save queued");
  return true;
#endif
}

bool camera_action_is_ui_active() {
#if !defined(ENABLE_CAMERA)
  return false;
#else
  lock_action();
  ActionState st = s_state;
  unlock_action();
  return st != ActionState::Idle && st != ActionState::Done;
#endif
}

void camera_action_on_touch(int16_t x, int16_t y) {
#if !defined(ENABLE_CAMERA)
  (void)x; (void)y;
#else
  lock_action();
  ActionState st = s_state;
  unlock_action();
  if (st != ActionState::ReviewSave) return;

  const int w = M5.Display.width();
  const int top = bar_top();
  if (y < top) return;
  bool left = x < (w / 2);

  if (left) {
    if (!commit_save()) {
      lock_action();
      s_result = "{\"error\":\"사진을 저장하지 못했어요.\"}";
      s_state = ActionState::Done;
      unlock_action();
      cleanup_temp();
      finish_idle_ui();
      return;
    }
  } else {
    s_saved = false;
    cleanup_temp();
  }
  lock_action();
  s_result = build_result();
  s_state = ActionState::Done;
  unlock_action();
  finish_idle_ui();
#endif
}

void camera_action_process_pending() {
#if defined(ENABLE_CAMERA)
  lock_action();
  ActionState st = s_state;
  if (st == ActionState::Countdown) {
    if (millis() < s_countdownNextMs) {
      unlock_action();
      return;
    }
    if (s_countdownDigit > 1) {
      s_countdownDigit--;
      s_countdownNextMs = millis() + 1000;
      int digit = s_countdownDigit;
      unlock_action();
      show_countdown_digit(digit);
      return;
    }
    s_state = ActionState::Running;
    unlock_action();
    avatar.setSpeechText("");
    Serial.println("[camera-action] capture started on loop task");
    bool ok = capture_preview();
    lock_action();
    if (s_abandonWhenDone) {
      s_result = "";
      s_state = ActionState::Idle;
      s_abandonWhenDone = false;
      unlock_action();
      cleanup_temp();
      finish_idle_ui();
      return;
    }
    if (!ok) {
      s_result = "{\"error\":\"사진 촬영에 실패했어요.\"}";
      s_state = ActionState::Done;
      unlock_action();
      finish_idle_ui();
      return;
    }
    s_state = ActionState::ReviewSave;
    unlock_action();
    show_save_ui();
    return;
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
  s_tempPath = "";
  s_savedPath = "";
  s_saved = false;
  unlock_action();
  return true;
#endif
}

void camera_action_abandon() {
#if defined(ENABLE_CAMERA)
  bool cleanupPreview = false;
  bool restoreUi = false;
  lock_action();
  if (s_state == ActionState::Running) {
    s_abandonWhenDone = true;
  } else {
    cleanupPreview = !s_saved && s_tempPath.length();
    restoreUi = s_state == ActionState::ReviewSave;
    s_result = "";
    s_state = ActionState::Idle;
    s_abandonWhenDone = false;
    s_countdownDigit = 0;
    s_tempPath = "";
    s_savedPath = "";
    s_saved = false;
  }
  unlock_action();
  if (cleanupPreview) {
    sd_bus_lock();
    SD.remove(kPreviewPath);
    sd_bus_unlock();
  }
  if (restoreUi) finish_idle_ui();
#endif
}
