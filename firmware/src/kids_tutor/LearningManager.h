#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "Question.h"
#include "QuestionDB.h"
#include "AdaptiveCurriculum.h"
#include "TutorConfig.h"
#include "share/SpiRamStlAllocator.h"

class LearningManager {
public:
  bool begin(fs::FS& fs, QuestionDB& englishDb, QuestionDB& mathDb, QuestionDB& curriculumDb);
  bool startDaily(uint8_t startLevel);

  // Picks the next question for the 10-minute daily routine.
  bool nextDailyQuestion(uint8_t level, const String& avoidId, Question& out,
                         bool& isEnglish, bool& isReview, String& phaseLabel);

  void noteWrongAttempt(const Question& q, bool isEnglish, const String& heard);
  void noteQuestionFinished(const Question& q, bool isEnglish, bool isReview,
                            bool finalCorrect, uint8_t attempts, const String& heard);

  bool sessionExpired() const;
  uint32_t remainingSeconds() const;
  String currentPhaseLabel() const;
  bool finishDaily(uint8_t endLevel, uint32_t stars);

  String studentName() const { return _studentName; }
  String studentAsciiName() const { return _studentAsciiName; }
  uint8_t studentAge() const { return _studentAge; }
  bool freeMath6yo() const { return _freeMath6yo; }
  uint8_t maxLevel() const { return _maxLevel; }
  uint8_t startLevel() const { return _startLevelConfig; }
  String adaptiveLevelEnglish() const { return _adaptive.currentLevelCode("english"); }
  String adaptiveLevelSoma() const { return _adaptive.currentLevelCode("math_somamath"); }
  String adaptiveLevelFacto() const { return _adaptive.currentLevelCode("math_facto"); }
  uint32_t answeredCount() const { return _answered; }
  uint32_t firstTryCorrect() const { return _firstTryCorrect; }
  uint32_t reviewCount() const { return _reviewAnswered; }
  String latestReportPath() const { return _latestReportPath; }
  String introSpeech() const;
  String greeting() const;
  String completionSpeech() const;

private:
  struct ReviewItem {
    String id;
    bool english = true;
    uint8_t stage = 0;      // 0=today, 1=1d, 2=3d, 3=7d, 4=14d
    uint16_t lapses = 0;
    uint32_t dueEpoch = 0;
  };

  struct DomainStat {
    String key;
    uint16_t total = 0;
    uint16_t firstTry = 0;
    uint16_t finalCorrect = 0;
    uint16_t attempts = 0;
  };

  struct MistakeLog {
    String id;
    String subject;
    String domain;
    String question;
    String answer;
    String heard;
    uint8_t attempts = 0;
  };

  fs::FS* _fs = nullptr;
  QuestionDB* _english = nullptr;
  QuestionDB* _math = nullptr;
  QuestionDB* _curriculum = nullptr;
  AdaptiveCurriculum _adaptive;

  String _studentName = "지우";
  String _studentAsciiName = "JIWOO";
  uint8_t _studentAge = 5;
  bool _freeMath6yo = false;
  uint8_t _maxLevel = 3;
  uint8_t _startLevelConfig = START_LEVEL;
  uint32_t _dailySeconds = 600;
  uint32_t _warmupSeconds = 60;
  uint32_t _englishSeconds = 180;
  uint32_t _somaSeconds = 180;
  uint32_t _factoSeconds = 120;
  uint32_t _finalReviewSeconds = 60;

  uint32_t _sessionStartMs = 0;
  uint32_t _sessionStartEpoch = 0;
  uint8_t _startLevel = 1;
  // Daily mode drives EN/K/P independently, so these are what actually moved.
  String _startCodeEnglish;
  String _startCodeSoma;
  String _startCodeFacto;
  bool _dailyActive = false;
  bool _finished = false;
  uint32_t _questionSerial = 0;

  uint32_t _answered = 0;
  uint32_t _firstTryCorrect = 0;
  uint32_t _finalCorrect = 0;
  uint32_t _englishAnswered = 0;
  uint32_t _englishFirstTry = 0;
  uint32_t _mathAnswered = 0;
  uint32_t _mathFirstTry = 0;
  uint32_t _reviewAnswered = 0;

  SpiRamVector<ReviewItem> _review;
  SpiRamVector<String> _sessionMistakes;
  SpiRamVector<DomainStat> _domainStats;
  SpiRamVector<MistakeLog> _mistakeLogs;
  String _latestReportPath;

  bool loadStudentConfig();
  bool loadCurriculumConfig();
  bool loadReviewQueue();
  bool saveReviewQueue();
  bool loadReviewQuestion(Question& out, bool& isEnglish, bool preferSessionMistake);
  void recordMistake(const Question& q, bool isEnglish);
  void recordReviewCorrect(const String& id);
  void addSessionMistake(const String& id);
  void removeSessionMistake(const String& id);
  ReviewItem* findReview(const String& id);
  DomainStat& domainStat(const String& key);

  uint32_t nowEpoch() const;
  String dateKey() const;
  String timestampText() const;
  bool chooseAdaptiveQuestion(const String& category, const String& avoidId, Question& out, bool& isReview);

  bool writeReports(uint8_t endLevel, uint32_t stars);
  String recommendationText() const;
  String weakestDomain() const;
};
