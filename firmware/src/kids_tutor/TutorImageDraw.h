#pragma once
#include <Arduino.h>
#include <M5Unified.h>

// Draw kids_tutor math illustrations with LCD primitives (no SD PNG).
// Specs are keyed by image basename, e.g. "fk_pattern_1" from
// "/kids_tutor/images/fk_pattern_1.png".

enum class TutorImgKind : uint8_t { Pattern, Count, Single, Bars };
enum class TutorImgShape : uint8_t { Circle, Square, Triangle };

struct TutorImageSpec {
  const char* id;          // basename without .png
  TutorImgKind kind;
  uint8_t colors[5];       // 0=R 1=G 2=B 3=P 4=O 5=Y
  TutorImgShape shape;
  uint8_t a;               // Count: n items; Bars: left height 1..5
  uint8_t b;               // Bars: right height 1..5
};

// Returns true if a procedural illustration was drawn into [x,y,w,h].
bool tutor_image_draw(const String& imagePath, int x, int y, int w, int h);
bool tutor_image_has_spec(const String& imagePath);
