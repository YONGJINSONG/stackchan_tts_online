#include <Arduino.h>
#include <SD.h>
#include <Avatar.h>
#include "KidsTutorMod.h"
#include "mod/ModManager.h"
#include "usage_timer.h"
#include "share/SdBus.h"
#include "QuestionDB.h"
#include "StackchanUI.h"
#include "LearningManager.h"
#include "TutorConfig.h"
#include "Robot.h"
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
}

KidsTutorMod::KidsTutorMod() {
  modName = "KidsTutor";
  g_kidsTutorMod = this;
}

bool KidsTutorMod::ensureReady(String& errOut) {
  if (_engineReady) return true;

  Serial.println("[kids] init: checking SD content");
  sd_bus_lock();
  bool hasRoot = SD.exists("/kids_tutor") || SD.exists("/kids_tutor/db");
  if (!hasRoot) {
    sd_bus_unlock();
    errOut = "SD에 /kids_tutor 폴더가 없어요. 공부 프로그램을 카드에 복사해 주세요.";
    return false;
  }

  bool okEng = g_englishDb.begin(SD, ENGLISH_DB_PATH, ENGLISH_IDX_PATH);
  bool okMath = g_mathDb.begin(SD, MATH_DB_PATH, MATH_IDX_PATH);
  bool okCur = g_curriculumDb.begin(SD, CURRICULUM_DB_PATH, CURRICULUM_IDX_PATH);
  g_math6Loaded = g_math6Db.begin(SD, MATH6_DB_PATH, MATH6_IDX_PATH);
  sd_bus_unlock();

  if (!okEng || !okMath || !okCur) {
    errOut = "공부 DB를 열지 못했어요. /kids_tutor/db 를 확인해 주세요.";
    return false;
  }

  Serial.println("[kids] init: databases loaded");
  g_ui.begin();
  Serial.println("[kids] init: voice settings");
  g_ui.beginVoice(SD);
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
  pauseUsageTimer();
  _session = g_tutor.start(subject);
  if (!_session) {
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
  g_avatar_render_pause = true;
  uint32_t pauseStart = millis();
  while (!g_avatar_sd_paused && millis() - pauseStart < 300) delay(2);
  Serial.println("[kids] init: avatar renderer paused");

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
  g_avatar_render_pause = false;
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

#if defined(REALTIME_API)
  // Let the function-output follow-up response finish before pausing the
  // Realtime mode. This also leaves the shared I2S audio state clean.
  if (robot && robot->llm && ((RealtimeLLMBase*)robot->llm)->isSpeaking()) return;
#endif

  TutorEngine::Subject subject;
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
