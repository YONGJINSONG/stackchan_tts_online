#include "StackchanUI.h"
#include "TutorImageDraw.h"

namespace {
constexpr int kTutorFooterHeight = 52;
constexpr int kExitBtnW = 70;
constexpr int kExitBtnH = 24;
}

void StackchanUI::prepareText() {
  // Avatar balloons leave MC_DATUM on M5.Lcd. Tutor layout is top-left.
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setFont(&fonts::efontKR_16);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void StackchanUI::begin() {
  // Host firmware already called M5.begin(); only claim the display here.
  prepareText();
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
  prepareText();
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

void StackchanUI::drawWrapped(const String& text, int x, int y, int maxWidth, int lineHeight, int maxLines) {
  int pos = 0;
  const int length = text.length();
  for (int line = 0; line < maxLines && pos < length; ++line) {
    String part;
    while (pos < length) {
      uint8_t first = (uint8_t)text[pos];
      int bytes = (first < 0x80) ? 1 : ((first & 0xE0) == 0xC0) ? 2 :
                  ((first & 0xF0) == 0xE0) ? 3 : 4;
      if (pos + bytes > length) bytes = 1;
      String candidate = part + text.substring(pos, pos + bytes);
      if (part.length() && M5.Display.textWidth(candidate) > maxWidth) break;
      part = candidate;
      pos += bytes;
    }
    part.trim();
    if (line == maxLines - 1 && pos < length) {
      const String ellipsis = "...";
      while (part.length() && M5.Display.textWidth(part + ellipsis) > maxWidth) {
        int cut = part.length() - 1;
        while (cut > 0 && (((uint8_t)part[cut] & 0xC0) == 0x80)) cut--;
        part.remove(cut);
      }
      part += ellipsis;
    }
    M5.Display.drawString(part, x, y + line * lineHeight);
    while (pos < length && text[pos] == ' ') pos++;
  }
}

void StackchanUI::drawTouchFooter(bool quiz) {
  const int w = M5.Display.width();
  const int h = M5.Display.height();
  const int top = h - kTutorFooterHeight;
  const int third = w / 3;
  const char* labels[3] = { quiz ? "이전" : "매일", quiz ? "정답" : "영어", quiz ? "다음" : "수학" };
  M5.Display.fillRect(0, top, w, h - top, TFT_BLACK);
  const int buttonTop = top + 3;
  const int buttonHeight = h - buttonTop - 3;
  for (int i = 0; i < 3; i++) {
    int left = i * third + 3;
    int right = (i == 2) ? w - 3 : (i + 1) * third - 3;
    M5.Display.drawRoundRect(left, buttonTop, right - left, buttonHeight, 5, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int labelX = left + max(2, ((right - left) - M5.Display.textWidth(labels[i])) / 2);
    M5.Display.drawString(labels[i], labelX, buttonTop + max(1, (buttonHeight - 16) / 2));
  }
}

void StackchanUI::drawExitButton() {
  const int w = M5.Display.width();
  const int left = w - kExitBtnW - 4;
  M5.Display.fillRoundRect(left, 2, kExitBtnW, kExitBtnH, 4, TFT_DARKGREY);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  const char* label = "나가기";
  int labelX = left + max(2, (kExitBtnW - M5.Display.textWidth(label)) / 2);
  M5.Display.drawString(label, labelX, 6);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

bool StackchanUI::hitExitButton(int16_t x, int16_t y) const {
  const int w = M5.Display.width();
  const int left = w - kExitBtnW - 4;
  return x >= left && x < w - 2 && y >= 0 && y < (kExitBtnH + 6);
}

void StackchanUI::showBootMenu(const String& learner, uint8_t age, bool math6yo) {
  prepareText();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawString("KIDS TUTOR", 10, 10);
  M5.Display.drawString(learner + "  " + String(age) + "세", 10, 36);
  M5.Display.drawString("화면 아래를 눌러 과목을 골라요", 10, 66);
  M5.Display.drawString(math6yo ? "수학: 6세 과정" : "수학: 자유 학습", 10, 94);
  M5.Display.drawString(localAudioReady() ? "소리: 로컬" : "소리: 기본", 10, 122);
  M5.Display.drawString("오른쪽 위 나가기 → 대화", 10, 150);
  drawTouchFooter(false);
  drawExitButton();
}

void StackchanUI::showStatusLine(const String& subject, uint8_t level, uint32_t stars,
                                 uint8_t questionNumber, uint8_t questionTotal,
                                 uint32_t remainingSeconds) {
  prepareText();
  M5.Display.fillRect(0, 0, M5.Display.width(), 28, TFT_BLACK);
  String status = subject + " " + String(questionNumber) + "/" + String(questionTotal)
                + " L" + String(level) + " *" + String(stars);
  if (remainingSeconds != 0xffffffffUL) {
    status += " " + String(remainingSeconds / 60) + ":"
            + String((remainingSeconds % 60 + 100)).substring(1);
  }
  drawWrapped(status, 8, 5, M5.Display.width() - kExitBtnW - 20, 16, 1);
  M5.Display.drawFastHLine(0, 28, M5.Display.width() - kExitBtnW - 8, TFT_DARKGREY);
  drawExitButton();
}

void StackchanUI::showQuestion(const Question& q, const std::vector<String>& choices, int selected,
                               const String& subject, uint8_t level, uint32_t stars,
                               uint8_t questionNumber, uint8_t questionTotal,
                               uint32_t remainingSeconds) {
  prepareText();
  M5.Display.fillScreen(TFT_BLACK);
  showStatusLine(subject, level, stars, questionNumber, questionTotal, remainingSeconds);

  // Prefer procedural LCD drawing (no SD) so CoreS3 avoids SD+LCD bus clashes.
  // Fallback: non-CoreS3 may still draw PNG from SD beside the question text.
  const bool hasProc = tutor_image_has_spec(q.image);
#if defined(ARDUINO_M5STACK_CORES3)
  const bool hasPng = false;
#else
  const bool hasPng = !hasProc && _fs && q.image.length() && _fs->exists(q.image);
#endif
  const int width = M5.Display.width();
  const int footerTop = M5.Display.height() - kTutorFooterHeight;
  const int questionTop = 32;
  const int rowCount = 3;
  // Reserve space for choices; with a figure, leave a centered band between
  // question text and the choice block.
  const int choiceBlock = hasProc ? 84 : 96;
  const int choiceY = max(questionTop + (hasProc ? 36 : 48), footerTop - choiceBlock);
  const int figH = hasProc ? max(40, choiceY - questionTop - 40) : 0;
  const int figTop = hasProc ? (questionTop + 34) : 0;
  int textX = 8;
  int textWidth = width - 16;
  int questionLines = hasProc ? 2 : max(1, min(3, (choiceY - questionTop - 4) / 16));

  if (hasProc) {
    drawWrapped(q.question, textX, questionTop, textWidth, 16, questionLines);
    tutor_image_draw(q.image, 8, figTop, width - 16, figH);
  } else if (hasPng) {
    const int questionHeight = choiceY - questionTop - 4;
    const int imgMax = min(56, questionHeight);
    const int imgX = 8;
    M5.Display.drawPngFile(q.image.c_str(), imgX, questionTop, imgMax, imgMax);
    textX = imgX + imgMax + 8;
    textWidth = width - textX - 8;
    drawWrapped(q.question, textX, questionTop, textWidth, 16, questionLines);
  } else {
    drawWrapped(q.question, textX, questionTop, textWidth, 16, questionLines);
  }

  const int choiceHeight = max(24, (footerTop - choiceY) / rowCount);
  int y = choiceY;
  for (size_t i = 0; i < choices.size() && i < 3; ++i) {
    if ((int)i == selected) {
      M5.Display.fillRoundRect(4, y, width - 8, choiceHeight - 2, 4, TFT_DARKGREY);
      M5.Display.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    } else {
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    drawWrapped(String(i+1) + ". " + choices[i], 10, y + 1, width - 20, 15, 2);
    y += choiceHeight;
  }
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (y < footerTop) M5.Display.fillRect(0, y, M5.Display.width(), footerTop - y, TFT_BLACK);
  drawTouchFooter(true);
  drawExitButton();
}

void StackchanUI::showMessage(const String& title, const String& body) {
  prepareText();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawString(title, 10, 20);
  drawWrapped(body, 10, 58, M5.Display.width() - 20, 18, 7);
  drawExitButton();
}

void StackchanUI::showListening() {
  drawFace(FaceMood::Listening);
  M5.Speaker.begin();
  M5.Speaker.setVolume(TUTOR_TONE_VOLUME);
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
