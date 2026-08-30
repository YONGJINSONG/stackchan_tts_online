#ifndef _LAYERED_FACE_H
#define _LAYERED_FACE_H

#include <M5Unified.h>
#include <SD.h>
#include "Face.h"
#include "Expression.h"
#include "DrawContext.h"

// SD /face/{base,eyes,mouth,blush,fx}/*.png layered avatar.
// Base and the composite stay 320x240 RGB565. Overlay layers are cropped to
// the non-magenta bounding box and blit at that offset.
// Per-frame work is pushSprite only — drawPng in the avatar loop freezes input.

class LayeredFace : public m5avatar::Face {
public:
  LayeredFace();
  virtual ~LayeredFace();

  virtual void draw(m5avatar::DrawContext* ctx) override;

  bool isReady() const { return _ready; }

  static bool sdReady();

private:
  enum EyeId {
    EYE_CENTER = 0,
    EYE_HAPPY,
    EYE_ANGRY,
    EYE_SAD,
    EYE_SLEEPY,
    EYE_SURPRISED,
    EYE_COUNT
  };
  enum MouthId {
    MOUTH_IDLE = 0,
    MOUTH_SMILE,
    MOUTH_ANGRY,
    MOUTH_SAD,
    MOUTH_O,
    MOUTH_OPEN1,
    MOUTH_OPEN2,
    MOUTH_COUNT
  };
  enum BlushId { BLUSH_NONE = -1, BLUSH_NORMAL = 0, BLUSH_SHY, BLUSH_COUNT };
  enum FxId { FX_NONE = -1, FX_TEAR = 0, FX_ZZZ, FX_COUNT };

  // Magenta chroma key for transparent areas (not used in art outlines).
  static constexpr uint16_t kTrans = 0xF81F;

  struct LayerSprite {
    M5Canvas* spr = nullptr;
    int16_t x = 0;
    int16_t y = 0;
  };

  M5Canvas* _base = nullptr;
  LayerSprite _eyes[EYE_COUNT] = {};
  LayerSprite _mouths[MOUTH_COUNT] = {};
  LayerSprite _blush[BLUSH_COUNT] = {};
  LayerSprite _fx[FX_COUNT] = {};
  M5Canvas* _composite = nullptr;

  bool _ready = false;
  int _lastEye = -1;
  int _lastMouth = -1;
  int _lastBlush = -2;
  int _lastFx = -2;
  int _mouthBand = 0;  // hysteresis: 0 closed, 1 mid, 2 open

  M5Canvas* loadLayerSprite(const char* path, bool opaqueBase);
  LayerSprite loadOverlaySprite(const char* path);
  LayerSprite cropOverlay(M5Canvas* full, const char* path);
  void freeSprite(M5Canvas*& spr);
  void freeLayer(LayerSprite& layer);
  void loadAll();
  void pickLayers(m5avatar::Expression e, float mouthOpen,
                  EyeId& eye, MouthId& mouth, BlushId& blush, FxId& fx);
  void ensureComposite(int w, int h);
  void rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx);
  void blitLayer(M5Canvas* layer, bool opaque);
  void blitLayer(const LayerSprite& layer, bool opaque);
};

bool layered_face_active();
void layered_face_set_active(bool on);

#endif  // _LAYERED_FACE_H
