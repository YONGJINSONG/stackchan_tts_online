#include "LayeredFace.h"
#include "Balloon.h"
#include "Effect.h"
#include <esp_heap_caps.h>

using namespace m5avatar;

namespace {
Balloon g_balloon;
Effect g_effect;
BoundingRect g_br;
bool g_layered_active = false;
}  // namespace

bool layered_face_active() { return g_layered_active; }
void layered_face_set_active(bool on) { g_layered_active = on; }

bool LayeredFace::sdReady() {
  return SD.exists("/face/base/base.png") || SD.exists("/face/base/base.jpg");
}

bool LayeredFace::loadPng(const char* path, PngBuf& out) {
  out.clear();
  if (!SD.exists(path)) {
    Serial.printf("[layered-face] missing: %s\n", path);
    return false;
  }
  File f = SD.open(path, "r");
  if (!f) {
    Serial.printf("[layered-face] open failed: %s\n", path);
    return false;
  }
  size_t sz = f.size();
  if (sz == 0 || sz > 512 * 1024) {
    Serial.printf("[layered-face] bad size %u: %s\n", (unsigned)sz, path);
    f.close();
    return false;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.printf("[layered-face] alloc failed (%u): %s\n", (unsigned)sz, path);
    f.close();
    return false;
  }
  size_t got = f.read(buf, sz);
  f.close();
  if (got != sz) {
    Serial.printf("[layered-face] short read %u/%u: %s\n", (unsigned)got, (unsigned)sz, path);
    free(buf);
    return false;
  }
  out.data = buf;
  out.size = sz;
  Serial.printf("[layered-face] loaded %s (%u)\n", path, (unsigned)sz);
  return true;
}

void LayeredFace::loadAll() {
  if (!loadPng("/face/base/base.png", _base)) {
    loadPng("/face/base/base.jpg", _base);
  }
  if (!_base.ok()) {
    Serial.println("[layered-face] no base — not ready");
    _ready = false;
    return;
  }

  loadPng("/face/eyes/eye_center.png", _eyes[EYE_CENTER]);
  loadPng("/face/eyes/eye_happy.png", _eyes[EYE_HAPPY]);
  loadPng("/face/eyes/eye_angry.png", _eyes[EYE_ANGRY]);
  loadPng("/face/eyes/eye_sad.png", _eyes[EYE_SAD]);
  loadPng("/face/eyes/eye_sleepy.png", _eyes[EYE_SLEEPY]);
  loadPng("/face/eyes/eyes_surprised.png", _eyes[EYE_SURPRISED]);

  loadPng("/face/mouth/mouth_idle.png", _mouths[MOUTH_IDLE]);
  loadPng("/face/mouth/mouth_smile.png", _mouths[MOUTH_SMILE]);
  loadPng("/face/mouth/mouth_angry.png", _mouths[MOUTH_ANGRY]);
  loadPng("/face/mouth/mouth_sad.png", _mouths[MOUTH_SAD]);
  loadPng("/face/mouth/mouth_o.png", _mouths[MOUTH_O]);
  loadPng("/face/mouth/mouth_open1.png", _mouths[MOUTH_OPEN1]);
  loadPng("/face/mouth/mouth_open2.png", _mouths[MOUTH_OPEN2]);

  loadPng("/face/blush/blush_normal.png", _blush[BLUSH_NORMAL]);
  loadPng("/face/blush/blush_shy.png", _blush[BLUSH_SHY]);

  loadPng("/face/fx/tear.png", _fx[FX_TEAR]);
  loadPng("/face/fx/zzz.png", _fx[FX_ZZZ]);

  _ready = true;
  Serial.println("[layered-face] ready");
}

LayeredFace::LayeredFace() : Face() {
  loadAll();
}

LayeredFace::~LayeredFace() {
  _base.clear();
  for (int i = 0; i < EYE_COUNT; i++) _eyes[i].clear();
  for (int i = 0; i < MOUTH_COUNT; i++) _mouths[i].clear();
  for (int i = 0; i < BLUSH_COUNT; i++) _blush[i].clear();
  for (int i = 0; i < FX_COUNT; i++) _fx[i].clear();
  if (_composite) {
    _composite->deleteSprite();
    delete _composite;
    _composite = nullptr;
  }
}

void LayeredFace::ensureComposite(int w, int h) {
  if (_composite && _composite->width() == w && _composite->height() == h) return;
  if (_composite) {
    _composite->deleteSprite();
    delete _composite;
    _composite = nullptr;
  }
  _composite = new M5Canvas(&M5.Display);
  _composite->setPsram(true);
  _composite->setColorDepth(16);
  if (!_composite->createSprite(w, h)) {
    Serial.printf("[layered-face] composite sprite %dx%d failed\n", w, h);
    delete _composite;
    _composite = nullptr;
  }
  _lastEye = -1;  // force rebuild
}

void LayeredFace::blitPng(const PngBuf& png) {
  if (!_composite || !png.ok()) return;
  // Prefer PNG (alpha layers); JPG for opaque base fallback.
  if (!_composite->drawPng(png.data, png.size)) {
    _composite->drawJpg(png.data, png.size);
  }
}

void LayeredFace::pickLayers(Expression e, float mouthOpen,
                             EyeId& eye, MouthId& mouth, BlushId& blush, FxId& fx) const {
  eye = EYE_CENTER;
  mouth = MOUTH_IDLE;
  blush = BLUSH_NONE;
  fx = FX_NONE;

  switch (e) {
    case Expression::Happy:
      eye = EYE_HAPPY;
      mouth = MOUTH_SMILE;
      blush = BLUSH_SHY;
      break;
    case Expression::Angry:
      eye = EYE_ANGRY;
      mouth = MOUTH_ANGRY;
      break;
    case Expression::Sad:
      eye = EYE_SAD;
      mouth = MOUTH_SAD;
      fx = FX_TEAR;
      break;
    case Expression::Sleepy:
      eye = EYE_SLEEPY;
      mouth = MOUTH_IDLE;
      fx = FX_ZZZ;
      break;
    case Expression::Doubt:
      eye = EYE_SURPRISED;
      mouth = MOUTH_O;
      break;
    case Expression::Neutral:
    default:
      eye = EYE_CENTER;
      mouth = MOUTH_IDLE;
      blush = BLUSH_NORMAL;
      break;
  }

  // Lipsync overrides mouth when speaking.
  if (mouthOpen >= 0.55f) {
    mouth = _mouths[MOUTH_OPEN2].ok() ? MOUTH_OPEN2 : MOUTH_O;
  } else if (mouthOpen >= 0.15f) {
    mouth = _mouths[MOUTH_OPEN1].ok() ? MOUTH_OPEN1 : MOUTH_O;
  }
}

void LayeredFace::rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx) {
  if (!_composite) return;
  _composite->fillSprite(TFT_BLACK);
  blitPng(_base);

  if (blush >= 0 && blush < BLUSH_COUNT) blitPng(_blush[blush]);

  // Fall back to center eyes if selected missing.
  const PngBuf* eyePng = &_eyes[eye];
  if (!eyePng->ok()) eyePng = &_eyes[EYE_CENTER];
  blitPng(*eyePng);

  const PngBuf* mouthPng = &_mouths[mouth];
  if (!mouthPng->ok()) mouthPng = &_mouths[MOUTH_IDLE];
  blitPng(*mouthPng);

  if (fx >= 0 && fx < FX_COUNT) blitPng(_fx[fx]);

  _lastEye = eye;
  _lastMouth = mouth;
  _lastBlush = blush;
  _lastFx = fx;
}

void LayeredFace::draw(DrawContext* ctx) {
  if (!_ready) {
    Face::draw(ctx);
    return;
  }

  const int w = boundingRect->getWidth();
  const int h = boundingRect->getHeight();
  ensureComposite(w, h);
  if (!_composite) {
    Face::draw(ctx);
    return;
  }

  EyeId eye;
  MouthId mouth;
  BlushId blush;
  FxId fx;
  pickLayers(ctx->getExpression(), ctx->getMouthOpenRatio(), eye, mouth, blush, fx);

  if (eye != _lastEye || mouth != _lastMouth || blush != _lastBlush || fx != _lastFx) {
    rebuild(eye, mouth, blush, fx);
  }

  // Draw into Face sprite so balloon/effect/scale path matches stock Face.
  sprite->setColorDepth(ctx->getColorDepth() == 1 ? 1 : 16);
  if (sprite->width() != w || sprite->height() != h) {
    // initSprites should have created this; recreate if needed.
    sprite->deleteSprite();
    sprite->setColorDepth(16);
    sprite->createSprite(w, h);
  }
  _composite->pushSprite(sprite, 0, 0);

  g_balloon.draw(sprite, g_br, ctx);
  g_effect.draw(sprite, g_br, ctx);

  float scale = ctx->getScale();
  float rotation = ctx->getRotation();
  if (scale != 1.0f || rotation != 0) {
    if (tmpSprite->width() != M5.Display.width()) {
      tmpSprite->deleteSprite();
      tmpSprite->setColorDepth(16);
      tmpSprite->createSprite(M5.Display.width(), M5.Display.height());
    }
    tmpSprite->fillSprite(ctx->getColorPalette()->get(COLOR_BACKGROUND));
    sprite->pushRotateZoom(tmpSprite,
                           M5.Display.width() / 2 + offset_x,
                           M5.Display.height() / 2 + offset_y,
                           rotation, scale, scale);
    if (subWindow && subWindowPos) {
      BoundingRect rect = *subWindowPos;
      rect.setPosition(rect.getTop(), rect.getLeft() + offset_x);
      subWindow->draw(tmpSprite, rect, ctx);
    }
    tmpSprite->pushSprite(&M5.Display, 0, 0);
  } else {
    sprite->pushSprite(&M5.Display, boundingRect->getLeft(), boundingRect->getTop());
  }
}
