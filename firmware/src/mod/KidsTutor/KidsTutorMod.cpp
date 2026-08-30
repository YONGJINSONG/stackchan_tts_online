#include <Arduino.h>
#include <SD.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "KidsTutorMod.h"
#include "mod/ModManager.h"
#include "usage_timer.h"
#include "share/SdBus.h"
#include "kids_tutor/QuestionDB.h"
#include "kids_tutor/StackchanUI.h"
#include "kids_tutor/LearningManager.h"
#include "kids_tutor/TutorConfig.h"
#include "Robot.h"
#include "NightMode.h"
#include "Gesture.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;

namespace m5avatar {
extern volatile bool g_avatar_render_pause;
extern volatile bool g_avatar_sd_paused;
}

extern Avatar avatar;

namespace {
StackchanUI g_ui;
QuestionDB g_englishDb;
QuestionDB g_mathDb;
QuestionDB g_math6Db;
QuestionDB g_curriculumDb;
LearningManager g_learning;
TutorEngine g_tutor;
KidsTutorMod* g_kidsTutorMod = nullptr;
bool g_math6Loaded = false;
portMUX_TYPE g_voiceStartMux = portMUX_INITIALIZER_UNLOCKED;
bool g_voiceStartPending = false;
TutorEngine::Subject g_voiceStartSubject = TutorEngine::Subject::Daily;
uint32_t g_voiceStartQueuedAt = 0;
constexpr uint32_t kVoiceStartSpeakingWaitMs = 2000;
}

KidsTutorMod::KidsTutorMod() {
  modName = "KidsTutor";
  g_kidsTutorMod = this;
}

bool KidsTutorMod::ensureReady(String& errOut) {
  if (_engineReady) return true;

  Serial.println("[kids] init: checking SD content");
  Serial.flush();
  sd_bus_lock();
  bool hasRoot = SD.exists("/kids_tutor") || SD.exists("/kids_tutor/db");
  Serial.printf("[kids] init: SD root=%d\n", (int)hasRoot);
  if (!hasRoot) {
    sd_bus_unlock();
    errOut = "SD에 /kids_tutor 폴더가 없어요. 공부 프로그램을 카드에 복사해 주세요.";
    return false;
  }

  Serial.println("[kids] init: opening DBs");
  bool okEng = g_englishDb.begin(SD, ENGLISH_DB_PATH, ENGLISH_IDX_PATH);
  bool okMath = g_mathDb.begin(SD, MATH_DB_PATH, MATH_IDX_PATH);
  bool okCur = g_curriculumDb.begin(SD, CURRICULUM_DB_PATH, CURRICULUM_IDX_PATH);
  g_math6Loaded = g_math6Db.begin(SD, MATH6_DB_PATH, MATH6_IDX_PATH);
  sd_bus_unlock();
  Serial.printf("[kids] init: db eng=%d math=%d cur=%d math6=%d\n",
                (int)okEng, (int)okMath, (int)okCur, (int)g_math6Loaded);

  if (!okEng || !okMath || !okCur) {
    errOut = "공부 DB를 열지 못했어요. /kids_tutor/db 를 확인해 주세요.";
    return false;
  }

  Serial.println("[kids] init: databases loaded");
  g_ui.begin();
  if (M5.Mic.isEnabled()) M5.Mic.end();
  delay(40);
  while (M5.Speaker.isPlaying()) delay(2);
  M5.Speaker.end();
  Serial.println("[kids] init: voice settings");
  if (!g_ui.beginVoice(SD)) {
    Serial.println("[kids] voice lite unavailable; button menu still works");
  }
  Serial.println("[kids] init: learning manager");
  sd_bus_lock();
  bool learningReady = g_learning.begin(SD, g_englishDb, g_mathDb, g_curriculumDb);
  sd_bus_unlock();
  if (!learningReady) {
    errOut = "학습 설정을 읽지 못했어요.";
    return false;
  }
  g_tutor.begin(g_englishDb, g_mathDb, g_ui, g_learning, g_math6Loaded ? &g_math6Db : nullptr);
  _engineReady = true;
  Serial.printf("[kids] ready eng=%u math=%u cur=%u math6=%d\n",
                (unsigned)g_englishDb.size(), (unsigned)g_mathDb.size(),
                (unsigned)g_curriculumDb.size(), (int)g_math6Loaded);
  return true;
}

void KidsTutorMod::showMenu() {
  _session = false;
  if (!_engineReady) {
    g_ui.showMessage("KIDS TUTOR", "SD /kids_tutor 를 준비해 주세요.");
    return;
  }
  g_ui.showBootMenu(g_learning.studentAsciiName(), g_learning.studentAge(),
                    g_math6Loaded || g_learning.freeMath6yo());
}

void KidsTutorMod::beginSession(TutorEngine::Subject subject) {
  String err;
  if (!ensureReady(err)) {
    g_ui.showMessage("ERROR", err);
    return;
  }
  // Realtime has already released I2S for a deferred mode switch. End mic
  // before speaker: Speaker.end() while mic_task is in i2s_stop() panics.
  if (M5.Mic.isEnabled()) M5.Mic.end();
  delay(40);
  while (M5.Speaker.isPlaying()) delay(2);
  M5.Speaker.end();
  pauseUsageTimer();
  Serial.printf("[kids] beginSession subject=%d\n", (int)subject);
  // Keep the head at its configured origin for the entire question session.
  gesture_set_motion_hold(true, true);
  _session = g_tutor.start(subject);
  if (!_session) {
    gesture_set_motion_hold(false);
    g_ui.showMessage("NO QUESTION", "문제를 불러오지 못했어요.");
    delay(1200);
    showMenu();
    resumeUsageTimer();
  }
}

void KidsTutorMod::endSessionToMenu() {
  if (_session) {
    _session = false;
    resumeUsageTimer();
  }
  showMenu();
}

void KidsTutorMod::queuePendingSubject(TutorEngine::Subject subject) {
  _pending = subject;
  _hasPending = true;
}

void KidsTutorMod::init(void) {
  // Own the LCD while this mod is active (RoboEyes / avatar face off).
  night_mode_hold_day_brightness(true);
  g_avatar_render_pause = true;
  uint32_t pauseStart = millis();
  while (!g_avatar_sd_paused && millis() - pauseStart < 300) delay(2);
  Serial.println("[kids] init: avatar renderer paused");
  if (M5.Mic.isEnabled()) M5.Mic.end();
  delay(40);
  while (M5.Speaker.isPlaying()) delay(2);
  M5.Speaker.end();

  String err;
  if (!ensureReady(err)) {
    g_ui.begin();
    g_ui.showMessage("KIDS TUTOR", err);
    return;
  }

  if (_hasPending) {
    _hasPending = false;
    beginSession(_pending);
  } else {
    showMenu();
    g_ui.speak(g_learning.introSpeech(), "ko");
    showMenu();
  }
}

void KidsTutorMod::pause(void) {
  if (_session) {
    _session = false;
    resumeUsageTimer();
  }
  _hasPending = false;
  gesture_set_motion_hold(false);
  if (M5.Mic.isEnabled()) M5.Mic.end();
  delay(40);
  while (M5.Speaker.isPlaying()) delay(2);
  M5.Speaker.end();
  g_avatar_render_pause = false;
  night_mode_hold_day_brightness(false);
  avatar.setSpeechText("");
}

void KidsTutorMod::btnA_pressed(void) {
  if (!_session) beginSession(TutorEngine::Subject::Daily);
  else g_tutor.previousChoice();
}

void KidsTutorMod::btnB_pressed(void) {
  if (!_session) beginSession(TutorEngine::Subject::English);
  else g_tutor.submitChoice();
}

void KidsTutorMod::btnC_pressed(void) {
  if (!_session) {
    if (g_math6Loaded || (_engineReady && g_learning.freeMath6yo()))
      beginSession(TutorEngine::Subject::Math6);
    else
      beginSession(TutorEngine::Subject::Math);
  } else {
    g_tutor.nextChoice();
  }
}

void KidsTutorMod::display_touched(int16_t x, int16_t y) {
  if (g_ui.hitExitButton(x, y)) {
    Serial.println("[kids] exit button -> chatbot");
    change_mod_chatbot();
    return;
  }
  if (y < 32) return;  // top bar / 나가기 영역은 과목·보기 버튼이 아님
  // CoreS3 has no physical A/B/C buttons. Match the three on-screen controls:
  // menu = Daily / English / Math, quiz = Previous / Submit / Next.
  const int width = M5.Display.width();
  if (width <= 0) return;
  int section = (x * 3) / width;
  if (section < 0) section = 0;
  if (section > 2) section = 2;
  Serial.printf("[kids] touch x=%d y=%d section=%d session=%d\n",
                (int)x, (int)y, section, (int)_session);
  if (section == 0) btnA_pressed();
  else if (section == 1) btnB_pressed();
  else btnC_pressed();
}

void KidsTutorMod::idle(void) {
  if (!_session) return;
  if (g_tutor.sessionComplete()) {
    g_tutor.clearSessionComplete();
    // sessionComplete becomes true only after stagewin.wav finishes.
    gesture_set_motion_hold(false);
    bool danceQueued = gesture_dance();
    Serial.printf("[kids] stage win dance queued=%d\n", (int)danceQueued);
    endSessionToMenu();
    return;
  }
  g_tutor.tick();
  // Button-only answers — do not call pollVoice (Whisper/cloud STT).
}

bool KidsTutorMod::isBusy(void) {
  return _session;
}

static TutorEngine::Subject map_subject_name(const String& raw) {
  String n = raw;
  n.trim();
  n.toLowerCase();
  if (n == "english" || n == "영어" || n.indexOf("english") >= 0 || n.indexOf("영어") >= 0) {
    return TutorEngine::Subject::English;
  }
  if (n == "math6" || n == "6세" || n.indexOf("math6") >= 0 || n.indexOf("6세") >= 0) {
    return TutorEngine::Subject::Math6;
  }
  if (n == "math" || n == "수학" || n == "소마" || n.indexOf("math") >= 0
      || n.indexOf("수학") >= 0 || n.indexOf("소마") >= 0 || n.indexOf("팩토") >= 0) {
    return TutorEngine::Subject::Math;
  }
  // empty / daily / generic "공부" → Daily 10 min
  return TutorEngine::Subject::Daily;
}

String kids_tutor_start_by_name(const char* name) {
  if (g_kidsTutorMod == nullptr) {
    return String("공부 모드가 없어요.");
  }

  TutorEngine::Subject sub = map_subject_name(name ? String(name) : String());
  // This function is called on the Realtime WebSocket task. Do not touch SD,
  // display, audio, or ModManager here: RealtimeAiMod::pause() would suspend
  // that same task in the middle of its WebSocket callback.
  portENTER_CRITICAL(&g_voiceStartMux);
  g_voiceStartSubject = sub;
  g_voiceStartPending = true;
  g_voiceStartQueuedAt = millis();
  portEXIT_CRITICAL(&g_voiceStartMux);
  Serial.println("[kids] voice start queued");

  const char* label = "데일리 10분";
  if (sub == TutorEngine::Subject::English) label = "영어 자유학습";
  else if (sub == TutorEngine::Subject::Math) label = "수학 자유학습";
  else if (sub == TutorEngine::Subject::Math6) label = "6세 수학";
  return String(label) + "을 준비할게요.";
}

void kids_tutor_process_pending() {
  if (g_kidsTutorMod == nullptr) return;

  bool pending = false;
  TutorEngine::Subject subject = TutorEngine::Subject::Daily;
  uint32_t queuedAt = 0;
  portENTER_CRITICAL(&g_voiceStartMux);
  pending = g_voiceStartPending;
  subject = g_voiceStartSubject;
  queuedAt = g_voiceStartQueuedAt;
  portEXIT_CRITICAL(&g_voiceStartMux);
  if (!pending) return;

#if defined(REALTIME_API)
  // The WebSocket task that owns the audio mutex finishes the current response
  // and releases Speaker/Mic before clearing speaking. Never force that state
  // from this main-loop task: doing so can strand the non-recursive audio mutex.
  // If speaking stays true (function-only turn), wait briefly then proceed.
  if (robot && robot->llm) {
    RealtimeLLMBase* rt = (RealtimeLLMBase*)robot->llm;
    if (rt->isSpeaking()) {
      if ((uint32_t)(millis() - queuedAt) < kVoiceStartSpeakingWaitMs) {
        return;
      }
      Serial.println("[kids] speaking wait timed out; opening study anyway");
    }
  }
#endif

  portENTER_CRITICAL(&g_voiceStartMux);
  if (!g_voiceStartPending) {
    portEXIT_CRITICAL(&g_voiceStartMux);
    return;
  }
  subject = g_voiceStartSubject;
  g_voiceStartPending = false;
  portEXIT_CRITICAL(&g_voiceStartMux);

  Serial.println("[kids] opening queued study program on main loop");
  Serial.flush();
  ModBase* cur = get_current_mod();
  if (cur && cur->getName() == "KidsTutor") {
    Serial.println("[kids] transition: restarting active tutor");
    g_kidsTutorMod->pause();
    g_kidsTutorMod->queuePendingSubject(subject);
    g_kidsTutorMod->init();
  } else {
    Serial.printf("[kids] transition: current=%s -> KidsTutor\n",
                  cur ? cur->getName().c_str() : "(none)");
    g_kidsTutorMod->queuePendingSubject(subject);
    change_mod_named("KidsTutor");
  }
}
