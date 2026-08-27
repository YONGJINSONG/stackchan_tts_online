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

void LayeredFace::freeSprite(M5Canvas*& spr) {
  if (!spr) return;
  spr->deleteSprite();
  delete spr;
  spr = nullptr;
}

M5Canvas* LayeredFace::loadLayerSprite(const char* path, bool opaqueBase) {
  if (!SD.exists(path)) {
    Serial.printf("[layered-face] missing: %s\n", path);
    return nullptr;
  }
  File f = SD.open(path, "r");
  if (!f) {
    Serial.printf("[layered-face] open failed: %s\n", path);
    return nullptr;
  }
  size_t sz = f.size();
  if (sz == 0 || sz > 512 * 1024) {
    Serial.printf("[layered-face] bad size %u: %s\n", (unsigned)sz, path);
    f.close();
    return nullptr;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.printf("[layered-face] alloc failed (%u): %s\n", (unsigned)sz, path);
    f.close();
    return nullptr;
  }
  size_t got = f.read(buf, sz);
  f.close();
  if (got != sz) {
    Serial.printf("[layered-face] short read %u/%u: %s\n", (unsigned)got, (unsigned)sz, path);
    free(buf);
    return nullptr;
  }

  const int w = 320;
  const int h = 240;
  M5Canvas* spr = new M5Canvas(&M5.Display);
  spr->setPsram(true);
  spr->setColorDepth(16);
  if (!spr->createSprite(w, h)) {
    Serial.printf("[layered-face] sprite failed: %s\n", path);
    free(buf);
    delete spr;
    return nullptr;
  }
  if (opaqueBase) {
    spr->fillSprite(TFT_BLACK);
  } else {
    spr->fillSprite(kTrans);
  }
  bool ok = spr->drawPng(buf, sz);
  if (!ok) ok = spr->drawJpg(buf, sz);
  free(buf);
  if (!ok) {
    Serial.printf("[layered-face] decode failed: %s\n", path);
    freeSprite(spr);
    return nullptr;
  }
  Serial.printf("[layered-face] rasterized %s\n", path);
  return spr;
}

void LayeredFace::loadAll() {
  _base = loadLayerSprite("/face/base/base.png", true);
  if (!_base) _base = loadLayerSprite("/face/base/base.jpg", true);
  if (!_base) {
    Serial.println("[layered-face] no base — not ready");
    _ready = false;
    return;
  }

  _eyes[EYE_CENTER] = loadLayerSprite("/face/eyes/eye_center.png", false);
  _eyes[EYE_HAPPY] = loadLayerSprite("/face/eyes/eye_happy.png", false);
  _eyes[EYE_ANGRY] = loadLayerSprite("/face/eyes/eye_angry.png", false);
  _eyes[EYE_SAD] = loadLayerSprite("/face/eyes/eye_sad.png", false);
  _eyes[EYE_SLEEPY] = loadLayerSprite("/face/eyes/eye_sleepy.png", false);
  _eyes[EYE_SURPRISED] = loadLayerSprite("/face/eyes/eyes_surprised.png", false);

  _mouths[MOUTH_IDLE] = loadLayerSprite("/face/mouth/mouth_idle.png", false);
  _mouths[MOUTH_SMILE] = loadLayerSprite("/face/mouth/mouth_smile.png", false);
  _mouths[MOUTH_ANGRY] = loadLayerSprite("/face/mouth/mouth_angry.png", false);
  _mouths[MOUTH_SAD] = loadLayerSprite("/face/mouth/mouth_sad.png", false);
  _mouths[MOUTH_O] = loadLayerSprite("/face/mouth/mouth_o.png", false);
  _mouths[MOUTH_OPEN1] = loadLayerSprite("/face/mouth/mouth_open1.png", false);
  _mouths[MOUTH_OPEN2] = loadLayerSprite("/face/mouth/mouth_open2.png", false);

  _blush[BLUSH_NORMAL] = loadLayerSprite("/face/blush/blush_normal.png", false);
  _blush[BLUSH_SHY] = loadLayerSprite("/face/blush/blush_shy.png", false);

  _fx[FX_TEAR] = loadLayerSprite("/face/fx/tear.png", false);
  _fx[FX_ZZZ] = loadLayerSprite("/face/fx/zzz.png", false);

  _ready = true;
  Serial.println("[layered-face] ready (pre-rasterized)");
}

LayeredFace::LayeredFace() : Face() {
  loadAll();
}

LayeredFace::~LayeredFace() {
  freeSprite(_base);
  for (int i = 0; i < EYE_COUNT; i++) freeSprite(_eyes[i]);
  for (int i = 0; i < MOUTH_COUNT; i++) freeSprite(_mouths[i]);
  for (int i = 0; i < BLUSH_COUNT; i++) freeSprite(_blush[i]);
  for (int i = 0; i < FX_COUNT; i++) freeSprite(_fx[i]);
  freeSprite(_composite);
}

void LayeredFace::ensureComposite(int w, int h) {
  if (_composite && _composite->width() == w && _composite->height() == h) return;
  freeSprite(_composite);
  _composite = new M5Canvas(&M5.Display);
  _composite->setPsram(true);
  _composite->setColorDepth(16);
  if (!_composite->createSprite(w, h)) {
    Serial.printf("[layered-face] composite %dx%d failed\n", w, h);
    delete _composite;
    _composite = nullptr;
    return;
  }
  _lastEye = -1;
}

void LayeredFace::blitLayer(M5Canvas* layer, bool opaque) {
  if (!_composite || !layer) return;
  if (opaque) {
    layer->pushSprite(_composite, 0, 0);
  } else {
    layer->pushSprite(_composite, 0, 0, kTrans);
  }
}

void LayeredFace::pickLayers(Expression e, float mouthOpen,
                             EyeId& eye, MouthId& mouth, BlushId& blush, FxId& fx) {
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

  // Hysteresis so lipSync noise does not thrash rebuilds.
  if (_mouthBand == 0) {
    if (mouthOpen >= 0.22f) _mouthBand = 1;
    if (mouthOpen >= 0.60f) _mouthBand = 2;
  } else if (_mouthBand == 1) {
    if (mouthOpen < 0.10f) _mouthBand = 0;
    else if (mouthOpen >= 0.60f) _mouthBand = 2;
  } else {
    if (mouthOpen < 0.45f) _mouthBand = 1;
    if (mouthOpen < 0.10f) _mouthBand = 0;
  }

  if (_mouthBand == 2 && _mouths[MOUTH_OPEN2]) mouth = MOUTH_OPEN2;
  else if (_mouthBand == 2 && _mouths[MOUTH_O]) mouth = MOUTH_O;
  else if (_mouthBand == 1 && _mouths[MOUTH_OPEN1]) mouth = MOUTH_OPEN1;
  else if (_mouthBand == 1 && _mouths[MOUTH_O]) mouth = MOUTH_O;
  // band 0 keeps emotion mouth
}

void LayeredFace::rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx) {
  if (!_composite || !_base) return;
  blitLayer(_base, true);
  if (blush >= 0 && blush < BLUSH_COUNT) blitLayer(_blush[blush], false);

  M5Canvas* eyeSpr = _eyes[eye] ? _eyes[eye] : _eyes[EYE_CENTER];
  blitLayer(eyeSpr, false);

  M5Canvas* mouthSpr = _mouths[mouth] ? _mouths[mouth] : _mouths[MOUTH_IDLE];
  blitLayer(mouthSpr, false);

  if (fx >= 0 && fx < FX_COUNT) blitLayer(_fx[fx], false);

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

  // Prefer direct blit of cached composite — avoid per-frame PNG decode.
  // Still draw balloon on a scratch face sprite when speech text is set.
  const char* speech = ctx->getspeechText();
  bool hasSpeech = speech && speech[0];

  const bool needSub = subWindow && subWindow->isEnabled();
  if (!hasSpeech && ctx->getScale() == 1.0f && ctx->getRotation() == 0 && !needSub) {
    _composite->pushSprite(&M5.Display, boundingRect->getLeft(), boundingRect->getTop());
    return;
  }

  if (sprite->width() != w || sprite->height() != h) {
    sprite->deleteSprite();
    sprite->setColorDepth(16);
    sprite->createSprite(w, h);
  }
  _composite->pushSprite(sprite, 0, 0);
  g_balloon.draw(sprite, g_br, ctx);
  g_effect.draw(sprite, g_br, ctx);

  float scale = ctx->getScale();
  float rotation = ctx->getRotation();
  if (scale != 1.0f || rotation != 0 || needSub) {
    if (tmpSprite->width() != M5.Display.width()) {
      tmpSprite->deleteSprite();
      tmpSprite->setColorDepth(16);
      tmpSprite->createSprite(M5.Display.width(), M5.Display.height());
    }
    tmpSprite->fillSprite(ctx->getColorPalette()->get(COLOR_BACKGROUND));
    if (scale != 1.0f || rotation != 0) {
      sprite->pushRotateZoom(tmpSprite,
                             M5.Display.width() / 2 + offset_x,
                             M5.Display.height() / 2 + offset_y,
                             rotation, scale, scale);
    } else {
      sprite->pushSprite(tmpSprite, boundingRect->getLeft() + offset_x,
                         boundingRect->getTop() + offset_y);
    }
    if (needSub && subWindowPos) {
      BoundingRect rect = *subWindowPos;
      rect.setPosition(rect.getTop(), rect.getLeft() + offset_x);
      subWindow->draw(tmpSprite, rect, ctx);
    }
    tmpSprite->pushSprite(&M5.Display, 0, 0);
  } else {
    sprite->pushSprite(&M5.Display, boundingRect->getLeft(), boundingRect->getTop());
  }
}
