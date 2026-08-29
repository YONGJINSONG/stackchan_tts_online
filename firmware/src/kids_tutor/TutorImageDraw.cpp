#include "TutorImageDraw.h"

namespace {

constexpr uint16_t kPalette[6] = {
  0xE147,  // R ~ #E04030
  0x25EE,  // G ~ #20C070
  0x349A,  // B ~ #3090D0
  0x98B6,  // P ~ #9050B0
  0xE8E4,  // O ~ #E07020
  0xEE00,  // Y ~ #F0C000
};

#include "TutorImageSpecs.inc"

String basename_id(const String& path) {
  int slash = path.lastIndexOf('/');
  String name = (slash >= 0) ? path.substring(slash + 1) : path;
  int dot = name.lastIndexOf('.');
  if (dot > 0) name = name.substring(0, dot);
  return name;
}

const TutorImageSpec* find_spec(const String& id) {
  for (int i = 0; i < kTutorImageSpecCount; i++) {
    if (id.equals(kTutorImageSpecs[i].id)) return &kTutorImageSpecs[i];
  }
  return nullptr;
}

uint16_t color_of(uint8_t idx) {
  if (idx >= 6) idx = 0;
  return kPalette[idx];
}

void fill_triangle(int cx, int cy, int size, uint16_t col) {
  // Point-up triangle inscribed in size×size box centered at (cx,cy).
  int half = size / 2;
  int x0 = cx;
  int y0 = cy - half;
  int x1 = cx - half;
  int y1 = cy + half;
  int x2 = cx + half;
  int y2 = cy + half;
  M5.Display.fillTriangle(x0, y0, x1, y1, x2, y2, col);
}

void draw_shape(TutorImgShape sh, int cx, int cy, int size, uint16_t col) {
  int half = size / 2;
  switch (sh) {
    case TutorImgShape::Circle:
      M5.Display.fillCircle(cx, cy, half, col);
      break;
    case TutorImgShape::Square:
      M5.Display.fillRect(cx - half, cy - half, size, size, col);
      break;
    case TutorImgShape::Triangle:
      fill_triangle(cx, cy, size, col);
      break;
  }
}

void draw_pattern(const TutorImageSpec& s, int x, int y, int w, int h) {
  const int cells = 6;
  const int gap = 4;
  int cell = min((w - gap * (cells - 1)) / cells, h - 4);
  if (cell < 10) cell = 10;
  int total = cells * cell + (cells - 1) * gap;
  int left = x + (w - total) / 2;
  int top = y + (h - cell) / 2;
  for (int i = 0; i < 5; i++) {
    int cx = left + i * (cell + gap) + cell / 2;
    int cy = top + cell / 2;
    draw_shape(TutorImgShape::Square, cx, cy, cell - 2, color_of(s.colors[i]));
  }
  // "?" cell
  int qx = left + 5 * (cell + gap);
  int qy = top;
  M5.Display.drawRect(qx, qy, cell, cell, TFT_DARKGREY);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("?", qx + cell / 2, qy + cell / 2);
  M5.Display.setTextDatum(textdatum_t::top_left);
}

void draw_count(const TutorImageSpec& s, int x, int y, int w, int h) {
  int n = s.a;
  if (n < 1) n = 1;
  if (n > 12) n = 12;
  int cols = (n <= 4) ? n : ((n <= 6) ? 3 : ((n <= 9) ? 3 : 5));
  int rows = (n + cols - 1) / cols;
  int gap = 4;
  int cell = min((w - gap * (cols - 1)) / cols, (h - gap * (rows - 1)) / rows);
  if (cell < 8) cell = 8;
  if (cell > 28) cell = 28;
  int totalW = cols * cell + (cols - 1) * gap;
  int totalH = rows * cell + (rows - 1) * gap;
  int left = x + (w - totalW) / 2;
  int top = y + (h - totalH) / 2;
  uint16_t col = color_of(s.colors[0]);
  for (int i = 0; i < n; i++) {
    int r = i / cols;
    int c = i % cols;
    int cx = left + c * (cell + gap) + cell / 2;
    int cy = top + r * (cell + gap) + cell / 2;
    draw_shape(s.shape, cx, cy, cell - 2, col);
  }
}

void draw_single(const TutorImageSpec& s, int x, int y, int w, int h) {
  int size = min(w, h) - 8;
  if (size < 20) size = 20;
  if (size > 72) size = 72;
  draw_shape(s.shape, x + w / 2, y + h / 2, size, color_of(s.colors[0]));
}

void draw_bars(const TutorImageSpec& s, int x, int y, int w, int h) {
  int h1 = constrain((int)s.a, 1, 5);
  int h2 = constrain((int)s.b, 1, 5);
  int barW = min(36, (w - 24) / 2);
  int maxH = h - 8;
  int unit = maxH / 5;
  int left = x + (w - (barW * 2 + 16)) / 2;
  int base = y + h - 4;
  int bh1 = unit * h1;
  int bh2 = unit * h2;
  M5.Display.fillRect(left, base - bh1, barW, bh1, color_of(s.colors[0]));
  M5.Display.fillRect(left + barW + 16, base - bh2, barW, bh2, color_of(s.colors[1]));
  M5.Display.drawFastHLine(left - 4, base, barW * 2 + 24, TFT_DARKGREY);
}

}  // namespace

bool tutor_image_has_spec(const String& imagePath) {
  if (!imagePath.length()) return false;
  return find_spec(basename_id(imagePath)) != nullptr;
}

bool tutor_image_draw(const String& imagePath, int x, int y, int w, int h) {
  if (w < 8 || h < 8) return false;
  const TutorImageSpec* spec = find_spec(basename_id(imagePath));
  if (!spec) return false;
  M5.Display.fillRect(x, y, w, h, TFT_BLACK);
  switch (spec->kind) {
    case TutorImgKind::Pattern:
      draw_pattern(*spec, x, y, w, h);
      break;
    case TutorImgKind::Count:
      draw_count(*spec, x, y, w, h);
      break;
    case TutorImgKind::Single:
      draw_single(*spec, x, y, w, h);
      break;
    case TutorImgKind::Bars:
      draw_bars(*spec, x, y, w, h);
      break;
  }
  return true;
}
