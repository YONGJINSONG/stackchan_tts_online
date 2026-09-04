#include "LayeredFace.h"
#include "Balloon.h"
#include "Effect.h"
#include "PetReaction.h"
#include <cstring>
#include <esp_heap_caps.h>

using namespace m5avatar;

namespace {
Balloon g_balloon;
Effect g_effect;
BoundingRect g_br;
bool g_layered_active = false;

extern "C" const uint8_t blush_pet_png_start[] asm("blush_pet_png_start");
extern "C" const uint8_t blush_pet_png_end[] asm("blush_pet_png_end");

void seedPetBlushAsset() {
  static constexpr const char* kPath = "/face/blush/blush_pet.png";
  if (SD.exists(kPath)) return;

  const size_t size = static_cast<size_t>(blush_pet_png_end - blush_pet_png_start);
  if (size == 0 || size > 64 * 1024) {
    Serial.printf("[layered-face] blush_pet seed invalid size=%u\n", (unsigned)size);
    return;
  }

  File file = SD.open(kPath, FILE_WRITE);
  if (!file) {
    Serial.println("[layered-face] blush_pet seed open failed");
    return;
  }
  const size_t written = file.write(blush_pet_png_start, size);
  file.close();
  Serial.printf("[layered-face] blush_pet seed %s bytes=%u/%u\n",
                written == size ? "ok" : "failed",
                (unsigned)written, (unsigned)size);
}
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

void LayeredFace::freeLayer(LayerSprite& layer) {
  freeSprite(layer.spr);
  layer.x = 0;
  layer.y = 0;
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
  if (opaqueBase) {
    Serial.printf("[layered-face] rasterized %s 320x240\n", path);
  }
  return spr;
}

LayeredFace::LayerSprite LayeredFace::cropOverlay(M5Canvas* full, const char* path) {
  LayerSprite out;
  if (!full) return out;

  const int w = full->width();
  const int h = full->height();
  const uint16_t* src = static_cast<const uint16_t*>(full->getBuffer());
  if (!src || w <= 0 || h <= 0) {
    freeSprite(full);
    return out;
  }

  int minX = w;
  int minY = h;
  int maxX = -1;
  int maxY = -1;
  for (int y = 0; y < h; ++y) {
    const uint16_t* row = src + y * w;
    for (int x = 0; x < w; ++x) {
      if (row[x] == kTrans) continue;
      if (x < minX) minX = x;
      if (y < minY) minY = y;
      if (x > maxX) maxX = x;
      if (y > maxY) maxY = y;
    }
  }

  if (maxX < 0) {
    Serial.printf("[layered-face] empty overlay: %s\n", path);
    freeSprite(full);
    return out;
  }

  const int boxW = maxX - minX + 1;
  const int boxH = maxY - minY + 1;
  if (boxW == w && boxH == h) {
    out.spr = full;
    out.x = 0;
    out.y = 0;
    Serial.printf("[layered-face] rasterized %s 320x240 -> %dx%d @ (0,0)\n",
                  path, boxW, boxH);
    return out;
  }

  M5Canvas* crop = new M5Canvas(&M5.Display);
  crop->setPsram(true);
  crop->setColorDepth(16);
  if (!crop->createSprite(boxW, boxH)) {
    Serial.printf("[layered-face] crop sprite failed: %s %dx%d\n",
                  path, boxW, boxH);
    delete crop;
    freeSprite(full);
    return out;
  }

  uint16_t* dst = static_cast<uint16_t*>(crop->getBuffer());
  if (!dst) {
    freeSprite(crop);
    freeSprite(full);
    return out;
  }
  for (int y = 0; y < boxH; ++y) {
    memcpy(dst + y * boxW, src + (minY + y) * w + minX, (size_t)boxW * sizeof(uint16_t));
  }
  freeSprite(full);

  out.spr = crop;
  out.x = (int16_t)minX;
  out.y = (int16_t)minY;
  Serial.printf("[layered-face] rasterized %s 320x240 -> %dx%d @ (%d,%d)\n",
                path, boxW, boxH, (int)out.x, (int)out.y);
  return out;
}

LayeredFace::LayerSprite LayeredFace::loadOverlaySprite(const char* path) {
  return cropOverlay(loadLayerSprite(path, false), path);
}

void LayeredFace::loadAll() {
  seedPetBlushAsset();

  _base = loadLayerSprite("/face/base/base.png", true);
  if (!_base) _base = loadLayerSprite("/face/base/base.jpg", true);
  if (!_base) {
    Serial.println("[layered-face] no base — not ready");
    _ready = false;
    return;
  }

  _eyes[EYE_CENTER] = loadOverlaySprite("/face/eyes/eye_center.png");
  _eyes[EYE_HAPPY] = loadOverlaySprite("/face/eyes/eye_happy.png");
  _eyes[EYE_ANGRY] = loadOverlaySprite("/face/eyes/eye_angry.png");
  _eyes[EYE_SAD] = loadOverlaySprite("/face/eyes/eye_sad.png");
  _eyes[EYE_SLEEPY] = loadOverlaySprite("/face/eyes/eye_sleepy.png");
  _eyes[EYE_SURPRISED] = loadOverlaySprite("/face/eyes/eyes_surprised.png");
  _eyes[EYE_CLOSED] = loadOverlaySprite("/face/eyes/eye_closed.png");
  _eyes[EYE_LEFT] = loadOverlaySprite("/face/eyes/eye_left.png");
  _eyes[EYE_PUZZLED] = loadOverlaySprite("/face/eyes/eye_puzzled.png");
  _eyes[EYE_RIGHT] = loadOverlaySprite("/face/eyes/eye_right.png");

  _mouths[MOUTH_IDLE] = loadOverlaySprite("/face/mouth/mouth_idle.png");
  _mouths[MOUTH_SMILE] = loadOverlaySprite("/face/mouth/mouth_smile.png");
  _mouths[MOUTH_ANGRY] = loadOverlaySprite("/face/mouth/mouth_angry.png");
  _mouths[MOUTH_SAD] = loadOverlaySprite("/face/mouth/mouth_sad.png");
  _mouths[MOUTH_O] = loadOverlaySprite("/face/mouth/mouth_o.png");
  _mouths[MOUTH_OPEN1] = loadOverlaySprite("/face/mouth/mouth_open1.png");
  _mouths[MOUTH_OPEN2] = loadOverlaySprite("/face/mouth/mouth_open2.png");

  _blush[BLUSH_NORMAL] = loadOverlaySprite("/face/blush/blush_normal.png");
  _blush[BLUSH_SHY] = loadOverlaySprite("/face/blush/blush_shy.png");
  _blush[BLUSH_PET] = loadOverlaySprite("/face/blush/blush_pet.png");

  _fx[FX_TEAR] = loadOverlaySprite("/face/fx/tear.png");
  _fx[FX_ZZZ] = loadOverlaySprite("/face/fx/zzz.png");

  _ready = true;
  Serial.println("[layered-face] ready (pre-rasterized, overlays cropped)");
  Serial.printf("[layered-face] reactions puzzled=%d pet_blush=%d\n",
                _eyes[EYE_PUZZLED].spr ? 1 : 0,
                _blush[BLUSH_PET].spr ? 1 : 0);
  if (!_eyes[EYE_PUZZLED].spr) {
    Serial.println("[layered-face] eye_puzzled missing; shake fallback=surprised");
  }
  if (!_blush[BLUSH_PET].spr) {
    Serial.println("[layered-face] blush_pet missing; pet fallback=blush_shy");
  }
}

LayeredFace::LayeredFace() : Face() {
  loadAll();
}

LayeredFace::~LayeredFace() {
  freeSprite(_base);
  for (int i = 0; i < EYE_COUNT; i++) freeLayer(_eyes[i]);
  for (int i = 0; i < MOUTH_COUNT; i++) freeLayer(_mouths[i]);
  for (int i = 0; i < BLUSH_COUNT; i++) freeLayer(_blush[i]);
  for (int i = 0; i < FX_COUNT; i++) freeLayer(_fx[i]);
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

void LayeredFace::blitLayer(const LayerSprite& layer, bool opaque) {
  if (!_composite || !layer.spr) return;
  if (opaque) {
    layer.spr->pushSprite(_composite, layer.x, layer.y);
  } else {
    layer.spr->pushSprite(_composite, layer.x, layer.y, kTrans);
  }
}

void LayeredFace::pickLayers(Expression e, float mouthOpen, float eyeOpen,
                             float gazeHorizontal,
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
      // General AI/idle doubt keeps the original surprised design.  The
      // puzzled overlay is reserved for the short physical shake reaction.
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

  // The avatar's blink task already drives eyeOpenRatio. Prefer the cropped
  // closed-eye overlay while it is fully closed, then restore the selected
  // emotion or neutral gaze layer without allocating or decoding per frame.
  if (e == Expression::Doubt && pet_reaction_dizzy_active()) {
    eye = _eyes[EYE_PUZZLED].spr ? EYE_PUZZLED : EYE_SURPRISED;
  } else if (eyeOpen <= 0.15f && _eyes[EYE_CLOSED].spr) {
    eye = EYE_CLOSED;
  } else if (e == Expression::Neutral) {
    if (gazeHorizontal <= -0.20f && _eyes[EYE_LEFT].spr) {
      eye = EYE_LEFT;
    } else if (gazeHorizontal >= 0.20f && _eyes[EYE_RIGHT].spr) {
      eye = EYE_RIGHT;
    }
  }

  // PetReaction owns the timer, while LayeredFace owns the cached blush
  // overlay.  This makes a touch stroke visibly blush even if another emotion
  // temporarily changes the eye and mouth layers.
  if (pet_reaction_blush_active()) {
    blush = _blush[BLUSH_PET].spr ? BLUSH_PET : BLUSH_SHY;
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

  if (_mouthBand == 2 && _mouths[MOUTH_OPEN2].spr) mouth = MOUTH_OPEN2;
  else if (_mouthBand == 2 && _mouths[MOUTH_O].spr) mouth = MOUTH_O;
  else if (_mouthBand == 1 && _mouths[MOUTH_OPEN1].spr) mouth = MOUTH_OPEN1;
  else if (_mouthBand == 1 && _mouths[MOUTH_O].spr) mouth = MOUTH_O;
  // band 0 keeps emotion mouth
}

void LayeredFace::rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx) {
  if (!_composite || !_base) return;
  blitLayer(_base, true);
  if (blush >= 0 && blush < BLUSH_COUNT) blitLayer(_blush[blush], false);
  if (blush == BLUSH_SHY && _blush[BLUSH_SHY].spr == nullptr) {
    _composite->fillEllipse(72, 158, 25, 10, 0xF98F);
    _composite->fillEllipse(248, 158, 25, 10, 0xF98F);
  }

  const LayerSprite& eyeSpr = _eyes[eye].spr ? _eyes[eye] : _eyes[EYE_CENTER];
  blitLayer(eyeSpr, false);

  const LayerSprite& mouthSpr = _mouths[mouth].spr ? _mouths[mouth] : _mouths[MOUTH_IDLE];
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
  const Gaze gaze = ctx->getGaze();
  pickLayers(ctx->getExpression(), ctx->getMouthOpenRatio(), ctx->getEyeOpenRatio(),
             gaze.getHorizontal(), eye, mouth, blush, fx);

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
