#include "StackchanUI.h"

void StackchanUI::begin() {
  // Host firmware already called M5.begin(); only claim the display here.
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillScreen(TFT_BLACK);
#if TUTOR_ENABLE_SERVO
  _servo.begin(TUTOR_SERVO_X_PIN, TUTOR_SERVO_X_HOME, TUTOR_SERVO_X_OFFSET,
               TUTOR_SERVO_Y_PIN, TUTOR_SERVO_Y_HOME, TUTOR_SERVO_Y_OFFSET,
               ServoType::PWM);
  _servoReady = true;
#endif
  drawFace(FaceMood::Happy);
}

bool StackchanUI::beginVoice(fs::FS& fs) {
  _fs = &fs;
#if TUTOR_ENABLE_VOICE
  showMessage("VOICE LITE", "Loading local audio / input settings...");
  bool ok = _voice.begin(fs, VOICE_LITE_CONFIG_PATH);
  if (!ok) {
    Serial.println("[VOICE LITE] config unavailable: " + _voice.lastError());
    showMessage("BUTTON MODE", "Tutor still works with A/B/C buttons.");
    delay(900);
  }
  return ok;
#else
  return false;
#endif
}

void StackchanUI::drawFace(FaceMood mood) {
  M5.Display.fillScreen(TFT_BLACK);
  const int w = M5.Display.width();
  const int h = M5.Display.height();
  const int cy = h / 2 - 10;
  M5.Display.fillCircle(w/2 - 45, cy, 11, TFT_WHITE);
  M5.Display.fillCircle(w/2 + 45, cy, 11, TFT_WHITE);
  if (mood == FaceMood::Happy) {
    M5.Display.drawArc(w/2, cy+45, 34, 28, 15, 165, TFT_WHITE);
  } else if (mood == FaceMood::Sad) {
    M5.Display.drawArc(w/2, cy+70, 34, 28, 195, 345, TFT_WHITE);
  } else if (mood == FaceMood::Thinking) {
    M5.Display.fillRect(w/2-20, cy+44, 40, 4, TFT_WHITE);
    M5.Display.fillCircle(w/2+65, cy-20, 4, TFT_WHITE);
  } else if (mood == FaceMood::Listening) {
    M5.Display.drawCircle(w/2, cy+48, 16, TFT_WHITE);
    M5.Display.drawString("LISTENING...", w/2-38, cy+78);
  } else {
    M5.Display.fillRect(w/2-22, cy+45, 44, 4, TFT_WHITE);
  }
}

void StackchanUI::drawWrapped(const String& text, int x, int y, int maxChars, int lineHeight, int maxLines) {
  int start = 0;
  for (int line = 0; line < maxLines && start < (int)text.length(); ++line) {
    int end = min(start + maxChars, (int)text.length());
    if (end < (int)text.length()) {
      int space = text.lastIndexOf(' ', end);
      if (space > start + maxChars/2) end = space;
    }
    String part = text.substring(start, end);
    part.trim();
    M5.Display.drawString(part, x, y + line * lineHeight);
    start = end;
    while (start < (int)text.length() && text[start] == ' ') start++;
  }
}

void StackchanUI::showBootMenu(const String& learner, uint8_t age, bool math6yo) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawString("STACK-CHAN KIDS TUTOR", 14, 16);
  M5.Display.setTextSize(1);
  M5.Display.drawString(learner + "  AGE " + String(age), 20, 54);
  M5.Display.drawString("A: DAILY 10 MIN", 20, 82);
  M5.Display.drawString("B: English free", 20, 108);
  M5.Display.drawString(math6yo ? "C: Math 6yo" : "C: Math free", 20, 134);
  String v = "AUDIO:" + String(localAudioReady() ? "LOCAL" : "TONE") + "  INPUT:" + voiceModeLabel();
  M5.Display.drawString(v, 20, 168);
  M5.Display.drawString("Quiz: A prev / B OK / C next", 10, 205);
}

void StackchanUI::showStatusLine(const String& subject, uint8_t level, uint32_t stars) {
  M5.Display.fillRect(0, 0, M5.Display.width(), 24, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(subject + "  LV" + String(level) + "  *" + String(stars), 8, 5);
  M5.Display.drawFastHLine(0, 24, M5.Display.width(), TFT_DARKGREY);
}

void StackchanUI::showQuestion(const Question& q, const std::vector<String>& choices, int selected,
                               const String& subject, uint8_t level, uint32_t stars) {
  M5.Display.fillScreen(TFT_BLACK);
  showStatusLine(subject, level, stars);

  const bool hasImage = _fs && q.image.length() && _fs->exists(q.image);
  int textY = 34;
  int questionLines = 5;
  int choiceY = 132;
  if (hasImage) {
    const int imgMax = 112;
    const int imgX = (M5.Display.width() - imgMax) / 2;
    // M5GFX on this PlatformIO pinout expects a path (SD), not fs::FS& + path.
    M5.Display.drawPngFile(q.image.c_str(), imgX, 26, imgMax, imgMax);
    textY = 142;
    questionLines = 2;
    choiceY = 176;
  }

  drawWrapped(q.question, 8, textY, 38, 16, questionLines);
  int y = choiceY;
  for (size_t i = 0; i < choices.size() && i < 4; ++i) {
    if ((int)i == selected) {
      M5.Display.fillRoundRect(4, y-2, M5.Display.width()-8, 18, 4, TFT_DARKGREY);
      M5.Display.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    } else {
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    M5.Display.drawString(String(i+1) + ". " + choices[i], 10, y);
    y += hasImage ? 16 : 24;
  }
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void StackchanUI::showMessage(const String& title, const String& body) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawString(title, 10, 30);
  M5.Display.setTextSize(1);
  drawWrapped(body, 10, 80, 38, 20, 7);
}

void StackchanUI::showListening() {
  drawFace(FaceMood::Listening);
  M5.Speaker.begin();
  M5.Speaker.tone(1200, 70);
  delay(120);
  M5.Speaker.end();
}

void StackchanUI::showTranscript(const String& text) {
  showMessage("I HEARD", text);
}

bool StackchanUI::speak(const String& text, const String& languageHint) {
  Serial.print("[SAY] ");
  Serial.println(text);
#if TUTOR_ENABLE_VOICE
  if (_voice.speak(text, languageHint)) return true;
#endif
  return false;
}

bool StackchanUI::speakQuestion(const Question& q, const String& languageHint) {
#if TUTOR_ENABLE_VOICE
  return _voice.speakQuestion(q, languageHint);
#else
  (void)q; (void)languageHint; return false;
#endif
}

bool StackchanUI::listen(String& transcript, const String& languageHint) {
#if TUTOR_ENABLE_VOICE
  if (!_voice.inputReady()) return false;
  showListening();
  bool ok = _voice.listen(transcript, languageHint);
  if (ok) showTranscript(transcript);
  else {
    showMessage("VOICE", _voice.lastError());
    delay(650);
  }
  return ok;
#else
  (void)transcript; (void)languageHint;
  return false;
#endif
}

void StackchanUI::centerHead() {
#if TUTOR_ENABLE_SERVO
  if (_servoReady) _servo.moveXY(TUTOR_SERVO_X_HOME, TUTOR_SERVO_Y_HOME, 350);
#endif
}

void StackchanUI::nod() {
#if TUTOR_ENABLE_SERVO
  if (_servoReady) {
    _servo.moveY(TUTOR_SERVO_Y_HOME + 12, 180);
    _servo.moveY(TUTOR_SERVO_Y_HOME - 6, 180);
    _servo.moveY(TUTOR_SERVO_Y_HOME, 180);
  }
#endif
}

void StackchanUI::thinkMove() {
#if TUTOR_ENABLE_SERVO
  if (_servoReady) {
    _servo.moveX(TUTOR_SERVO_X_HOME + 12, 300);
    _servo.moveX(TUTOR_SERVO_X_HOME, 300);
  }
#endif
}
