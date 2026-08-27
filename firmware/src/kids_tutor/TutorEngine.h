#pragma once
#include <Arduino.h>
#include <vector>
#include "QuestionDB.h"
#include "StackchanUI.h"
#include "LearningManager.h"

class TutorEngine {
public:
  enum class Subject { Daily, English, Math, Math6, Spatial, Mixed };

  void begin(QuestionDB& englishDb, QuestionDB& mathDb, StackchanUI& ui, LearningManager& learning,
             QuestionDB* math6Db = nullptr, QuestionDB* spatialDb = nullptr);
  bool start(Subject subject);
  void previousChoice();
  void nextChoice();
  void submitChoice();
  void skip();
  void pollVoice();
  void requestVoiceRetry();
  void tick();

  const Question& current() const { return _current; }
  int selected() const { return _selected; }
  uint8_t level() const { return _level; }
  uint32_t stars() const { return _stars; }
  Subject subject() const { return _subject; }
  bool sessionComplete() const { return _sessionComplete; }
  void clearSessionComplete() { _sessionComplete = false; }

private:
  QuestionDB* _english = nullptr;
  QuestionDB* _math = nullptr;
  QuestionDB* _math6 = nullptr;
  QuestionDB* _spatial = nullptr;
  StackchanUI* _ui = nullptr;
  LearningManager* _learning = nullptr;
  Subject _subject = Subject::Daily;
  Question _current;
  std::vector<String> _displayChoices;
  int _selected = 0;
  uint8_t _level = 1;
  uint8_t _wrong = 0;
  uint8_t _streak = 0;
  uint32_t _stars = 0;
  bool _mixedEnglishNext = true;
  bool _voicePending = false;
  bool _grading = false;
  bool _currentEnglish = true;
  bool _currentIsReview = false;
  bool _sessionComplete = false;
  uint32_t _shownRemaining = 0xffffffffUL;
  String _dailyPhaseLabel = "DAILY";

  bool loadNext();
  bool finishDailyIfNeeded();
  void buildChoices();
  void render();
  void renderStatus();
  uint8_t currentLevelCap() const;
  void gradeAnswer(const String& answer, bool fromVoice);
  bool voiceMatches(const String& heard) const;
  bool answersEqual(const String& a, const String& b) const;
  String normalized(String s) const;
  String normalizeNumbers(const String& s) const;
  float similarity(const String& a, const String& b) const;
  String subjectName() const;
  String languageHint() const;
  bool isEnglishQuestion() const;
  QuestionDB* freeMathDb() const;
};
