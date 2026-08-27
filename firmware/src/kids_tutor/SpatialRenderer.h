#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include <vector>
#include "Question.h"

class SpatialRenderer {
public:
  static void drawQuestion(const Question& q,
                           const std::vector<String>& displayChoices,
                           int selected,
                           const String& subject,
                           uint8_t level,
                           uint32_t stars);

private:
  static void drawStatus(const String& subject, uint8_t level, uint32_t stars);
  static void drawFooter();
  static void drawWrapped(const String& text, int x, int y, int maxWidth, int lineHeight, int maxLines);
  static void drawGrid(const String& encoded, int cx, int cy, int maxW, int maxH, uint16_t color);
  static void drawPattern(const String& encoded, int x, int y, int w, int h);
  static void drawHeightMap(const String& encoded, int x, int y, int w, int h);
  static void drawToken(const String& token, int cx, int cy, int size, uint16_t color);
};
