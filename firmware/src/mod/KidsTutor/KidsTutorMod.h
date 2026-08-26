#ifndef _KIDS_TUTOR_MOD_H
#define _KIDS_TUTOR_MOD_H

#include <Arduino.h>
#include "mod/ModBase.h"
#include "TutorEngine.h"

// Start (or switch into) Kids Tutor from voice / web.
// name: empty/daily/데일리/10분/공부 → Daily; english/영어 → English;
//       math/수학/소마 → Math; math6/6세 → Math6.
// Returns a short Korean status string for Function Calling / TTS.
String kids_tutor_start_by_name(const char* name);

class KidsTutorMod : public ModBase {
public:
  KidsTutorMod();

  void init(void) override;
  void pause(void) override;
  void btnA_pressed(void) override;
  void btnB_pressed(void) override;
  void btnC_pressed(void) override;
  void idle(void) override;
  bool isBusy(void) override;

  // Called after change_mod_named when voice/web picked a subject.
  void queuePendingSubject(TutorEngine::Subject subject);
  bool ensureReady(String& errOut);

private:
  bool _session = false;
  bool _engineReady = false;
  bool _hasPending = false;
  TutorEngine::Subject _pending = TutorEngine::Subject::Daily;

  void showMenu();
  void beginSession(TutorEngine::Subject subject);
  void endSessionToMenu();
};

#endif  //_KIDS_TUTOR_MOD_H
