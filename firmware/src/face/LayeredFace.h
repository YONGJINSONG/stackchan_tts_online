#ifndef _LAYERED_FACE_H
#define _LAYERED_FACE_H

#include <M5Unified.h>
#include <SD.h>
#include "Face.h"
#include "Expression.h"
#include "DrawContext.h"

// SD /face/{base,eyes,mouth,blush,fx}/*.png layered avatar.
// Expects 320x240 assets (pre-resized). Composites base + blush + eyes + mouth + fx
// with mouthOpenRatio lipsync. Falls back to vector Face::draw if base missing.

class LayeredFace : public m5avatar::Face {
public:
  LayeredFace();
  virtual ~LayeredFace();

  virtual void draw(m5avatar::DrawContext* ctx) override;

  bool isReady() const { return _ready; }

  // True if /face/base/base.png (or .jpg) exists on SD.
  static bool sdReady();

private:
  struct PngBuf {
    uint8_t* data = nullptr;
    size_t size = 0;
    bool ok() const { return data != nullptr && size > 0; }
    void clear() {
      if (data) { free(data); data = nullptr; }
      size = 0;
    }
  };

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

  PngBuf _base;
  PngBuf _eyes[EYE_COUNT];
  PngBuf _mouths[MOUTH_COUNT];
  PngBuf _blush[BLUSH_COUNT];
  PngBuf _fx[FX_COUNT];

  M5Canvas* _composite = nullptr;
  bool _ready = false;

  int _lastEye = -1;
  int _lastMouth = -1;
  int _lastBlush = -2;
  int _lastFx = -2;

  bool loadPng(const char* path, PngBuf& out);
  void loadAll();
  void pickLayers(m5avatar::Expression e, float mouthOpen,
                  EyeId& eye, MouthId& mouth, BlushId& blush, FxId& fx) const;
  void ensureComposite(int w, int h);
  void rebuild(EyeId eye, MouthId mouth, BlushId blush, FxId fx);
  void blitPng(const PngBuf& png);
};

// Set true when main.cpp installs LayeredFace (RealtimeAiMod skips RoboEyes).
bool layered_face_active();
void layered_face_set_active(bool on);

#endif  // _LAYERED_FACE_H
