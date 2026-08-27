#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include <vector>
#include "Question.h"
#include "TutorConfig.h"
#include "VoiceLite.h"

#if TUTOR_ENABLE_SERVO
#include "Stackchan_servo.h"
#endif

enum class FaceMood { Neutral, Happy, Thinking, Sad, Listening };

class StackchanUI {
public:
  void begin();
  bool beginVoice(fs::FS& fs);
  bool voiceReady() const { return _voice.inputReady(); }
  bool voiceAutoListen() const { return _voice.autoListen(); }
  bool localAudioReady() const { return _voice.audioReady(); }
  String voiceModeLabel() const { return _voice.modeLabel(); }
  String voiceError() const { return _voice.lastError(); }

  void drawFace(FaceMood mood);
  void showBootMenu(const String& learner = "JIWOO", uint8_t age = 5, bool math6yo = false);
  void showQuestion(const Question& q, const std::vector<String>& choices, int selected,
                    const String& subject, uint8_t level, uint32_t stars,
                    uint8_t questionNumber, uint8_t questionTotal,
                    uint32_t remainingSeconds = 0xffffffffUL);
  // Redraws only the top strip so the daily countdown can update without the
  // full-screen repaint (and flicker) of showQuestion().
  void showStatusLine(const String& subject, uint8_t level, uint32_t stars,
                      uint8_t questionNumber, uint8_t questionTotal,
                      uint32_t remainingSeconds = 0xffffffffUL);
  void showMessage(const String& title, const String& body);
  void showListening();
  void showTranscript(const String& text);
  bool speak(const String& text, const String& languageHint = "en");
  bool speakQuestion(const Question& q, const String& languageHint = "en");
  bool listen(String& transcript, const String& languageHint = "en");
  void nod();
  void thinkMove();
  void centerHead();

private:
  fs::FS* _fs = nullptr;
  VoiceLite _voice;
#if TUTOR_ENABLE_SERVO
  StackchanSERVO _servo;
  bool _servoReady = false;
#endif
  void drawWrapped(const String& text, int x, int y, int maxWidth, int lineHeight, int maxLines);
  void drawTouchFooter(bool quiz);
};
