#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "Question.h"
#include "QuestionDB.h"

class AdaptiveCurriculum {
public:
  bool begin(fs::FS& fs, QuestionDB& db);
  void startSession();
  bool pickTrack(const String& category, const String& avoidId, uint32_t nowEpoch,
                 Question& out, bool& isReview);
  bool pickDueReview(uint32_t nowEpoch, bool preferSessionDue,
                     Question& out, String& category);
  void recordAnswer(const Question& q, bool firstTryCorrect, bool finalCorrect,
                    bool isReview, uint32_t nowEpoch);
  void flush();

  uint8_t currentLevel(const String& category) const;
  String currentLevelCode(const String& category) const;

private:
  struct TrackState {
    String category;
    uint8_t currentLevel = 1;
    uint8_t maxLevel = 1;
    std::vector<uint8_t> freshResults;
    std::vector<String> uniqueCurrent;
    uint32_t questionCount = 0;
    uint16_t levelChanges = 0;
  };

  struct LevelPool {
    String category;
    uint8_t level = 1;
    std::vector<uint16_t> indices;
  };

  struct ProblemState {
    String id;
    String category;
    uint8_t level = 1;
    uint8_t box = 1;
    uint16_t timesShown = 0;
    uint16_t lapses = 0;
    uint32_t dueEpoch = 0;
    uint32_t dueQuestion = 0;
    uint32_t lastQuestion = 0;
  };

  fs::FS* _fs = nullptr;
  QuestionDB* _db = nullptr;
  std::vector<TrackState> _tracks;
  std::vector<LevelPool> _pools;
  std::vector<ProblemState> _problems;
  std::vector<String> _recentIds;
  uint32_t _sessionSerial = 0;
  uint8_t _dirty = 0;

  // Runtime policy loaded from /kids_tutor/config/curriculum.json.
  uint8_t _window = 12;
  float _promoteAccuracy = 0.83f;
  float _demoteAccuracy = 0.42f;
  uint8_t _minUniqueForPromotion = 8;
  uint8_t _sessionGap = 2;
  uint8_t _reviewProbability = 25;
  uint8_t _recentGuard = 4;
  uint8_t _saveEvery = 5;
  uint16_t _reviewDays[4] = {1, 3, 7, 14};

  TrackState* track(const String& category);
  LevelPool* levelPool(const String& category, uint8_t level);
  const LevelPool* levelPool(const String& category, uint8_t level) const;
  void buildPools();
  const TrackState* track(const String& category) const;
  ProblemState* problem(const String& id);
  const ProblemState* problem(const String& id) const;
  bool isRecent(const String& id) const;
  bool isDue(const ProblemState& p, uint32_t nowEpoch) const;
  bool allowedLevel(const ProblemState& p) const;
  bool pickDueForCategory(const String& category, uint32_t nowEpoch,
                          Question& out, bool sessionOnly);
  bool pickFreshCurrent(const String& category, const String& avoidId, uint32_t nowEpoch, Question& out);
  bool pickLeastMasteredCurrent(const String& category, const String& avoidId, Question& out);
  void noteRecent(const String& id);
  void adjustLevel(TrackState& t);
  uint32_t intervalDays(uint8_t box) const;
  String levelCode(const String& category, uint8_t level) const;

  bool loadPolicyConfig();
  bool loadState();
  bool saveState();
};
