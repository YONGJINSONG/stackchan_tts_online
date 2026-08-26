#ifndef _LAYERED_FACE_H
#define _LAYERED_FACE_H

#include <M5Unified.h>
#include <SD.h>
#include "Face.h"
#include "Expression.h"
#include "DrawContext.h"

// SD /face/{base,eyes,mouth,blush,fx}/*.png layered avatar.
// Layers are decoded once at boot into RGB565 sprites (magenta = transparent).
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

  M5Canvas* _base = nullptr;
  M5Canvas* _eyes[EYE_COUNT] = {};
  M5Canvas* _mouths[MOUTH_COUNT] = {};
  M5Canvas* _blush[BLUSH_COUNT] = {};
  M5Canvas* _fx[FX_COUNT] = {};
  M5Canvas* _composite = nullptr;

  bool _ready = false;
  int _lastEye = -1;
  int _lastMouth = -1;
  int _lastBlush = -2;
  int _lastFx = -2;
  int _mouthBand = 0;  // hysteresis: 0 closed, 1 mid, 2 open

  M5Canvas* loadLayerSprite(const char* path, bool opaqueBase);
  void freeSprite(M5Canvas*& spr);
  void loadAll();
  void pickLayers(m5avatar::Expression e, float mouthOpen,
                  EyeId& eye, MouthId& mouth, BlushId& blush, FxId& fx);
  void ensureComposite(int w, int h);
  void rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx);
  void blitLayer(M5Canvas* layer, bool opaque);
};

bool layered_face_active();
void layered_face_set_active(bool on);

#endif  // _LAYERED_FACE_H
