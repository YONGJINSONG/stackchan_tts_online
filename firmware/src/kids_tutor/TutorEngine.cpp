#include "TutorEngine.h"
#include "TutorConfig.h"
#include <algorithm>
#include <vector>
#include <ctype.h>
#include <math.h>

void TutorEngine::begin(QuestionDB& englishDb, QuestionDB& mathDb, StackchanUI& ui, LearningManager& learning,
                        QuestionDB* math6Db) {
  _english = &englishDb;
  _math = &mathDb;
  _math6 = math6Db;
  _ui = &ui;
  _learning = &learning;
  _level = learning.startLevel();
}

uint8_t TutorEngine::currentLevelCap() const {
  uint8_t cap = _learning ? _learning->maxLevel() : MAX_LEVEL;
  if (cap < 1) cap = 1;
  if (cap > MAX_LEVEL) cap = MAX_LEVEL;

  // Never climb past what the database actually contains: math.ndjson stops at
  // level 2, so a higher level would only ever resolve through the fallback.
  uint8_t available = MAX_LEVEL;
  if (_subject == Subject::English && _english) available = _english->maxAvailableLevel();
  else if (_subject == Subject::Math6 && _math6) available = _math6->maxAvailableLevel();
  else if (_subject == Subject::Math) {
    QuestionDB* db = freeMathDb();
    if (db) available = db->maxAvailableLevel();
  } else if (_subject == Subject::Mixed && _english && _math)
    available = std::min(_english->maxAvailableLevel(), _math->maxAvailableLevel());
  return cap < available ? cap : available;
}

bool TutorEngine::start(Subject subject) {
  _subject = subject;
  _wrong = 0;
  _streak = 0;
  _stars = 0;
  _mixedEnglishNext = true;
  _voicePending = false;
  _sessionComplete = false;
  _shownRemaining = 0xffffffffUL;

  const uint8_t cap = currentLevelCap();
  if (_level > cap) _level = cap;
  if (_level < 1) _level = 1;

  if (_subject == Subject::Daily && _learning) {
    _learning->startDaily(_level);
    _ui->showMessage("10 MIN DAILY", _learning->studentAsciiName() + " / AGE " + String(_learning->studentAge()));
    _ui->speak(_learning->greeting(), "ko");
  }
  return loadNext();
}

String TutorEngine::normalizeNumbers(const String& input) const {
  String s = input;
  const char* words[] = {"zero","one","two","three","four","five","six","seven","eight","nine","ten",
                         "eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen","twenty"};
  for (int n=20; n>=0; --n) {
    String w = String(words[n]);
    if (s == w) s = String(n);
    s.replace(" " + w + " ", " " + String(n) + " ");
    if (s.startsWith(w + " ")) s = String(n) + s.substring(w.length());
    if (s.endsWith(" " + w)) s = s.substring(0, s.length()-w.length()) + String(n);
  }
  return s;
}

String TutorEngine::normalized(String s) const {
  s.toLowerCase();
  s.trim();
  s.replace("i'm", "i am");
  s.replace("it's", "it is");
  s.replace("you're", "you are");
  s.replace("that's", "that is");
  s.replace("isn't", "is not");
  s.replace("don't", "do not");
  s.replace("can't", "cannot");
  s.replace("i’d", "i would");
  s.replace("i'd", "i would");
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c >= 0x80 || c == ' ') out += (char)c;
  }
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  out.trim();
  return normalizeNumbers(out);
}

bool TutorEngine::answersEqual(const String& a, const String& b) const {
  return normalized(a) == normalized(b);
}

float TutorEngine::similarity(const String& aa, const String& bb) const {
  String a = normalized(aa), b = normalized(bb);
  if (a == b) return 1.0f;
  if (a.length() == 0 || b.length() == 0) return 0.0f;
  const int n = a.length(), m = b.length();
  std::vector<int> prev(m+1), cur(m+1);
  for (int j=0;j<=m;++j) prev[j]=j;
  for (int i=1;i<=n;++i) {
    cur[0]=i;
    for (int j=1;j<=m;++j) {
      int cost = a[i-1] == b[j-1] ? 0 : 1;
      cur[j] = std::min(std::min(cur[j-1]+1, prev[j]+1), prev[j-1]+cost);
    }
    prev.swap(cur);
  }
  int longest = std::max(n,m);
  return longest ? 1.0f - (prev[m] / (float)longest) : 1.0f;
}

bool TutorEngine::voiceMatches(const String& heard) const {
  String h = normalized(heard);
  if (h.length() == 0) return false;
  std::vector<String> expected = _current.speechAnswers;
  expected.push_back(_current.answer);
  for (const String& raw : expected) {
    String e = normalized(raw);
    if (e.length() == 0) continue;
    if (h == e) return true;
    if (e.length() >= 3 && (h.indexOf(e) >= 0 || e.indexOf(h) >= 0)) {
      int delta = abs((int)h.length() - (int)e.length());
      if (delta <= 12 || e.length() >= 6) return true;
    }
    float score = similarity(h,e);
    float threshold = VOICE_FUZZY_THRESHOLD;
    if (e.length() <= 3) threshold = 0.95f;
    else if (e.length() <= 5) threshold = 0.84f;
    if (score >= threshold) return true;
  }
  return false;
}

bool TutorEngine::isEnglishQuestion() const {
  if (_subject == Subject::Math || _subject == Subject::Math6) return false;
  if (_current.answerLanguage.length()) return _current.answerLanguage != "ko";
  if (_subject == Subject::Daily) return _currentEnglish;
  return !(_current.id.startsWith("SOMA") || _current.id.startsWith("FACTO") ||
           _current.id.startsWith("SM") || _current.id.startsWith("FK-") ||
           _current.id.startsWith("KF-") || _current.id.startsWith("PL-") ||
           _current.id.startsWith("WS-"));
}

QuestionDB* TutorEngine::freeMathDb() const {
  if (_learning && _learning->freeMath6yo() && _math6) return _math6;
  return _math;
}

String TutorEngine::languageHint() const {
  if (_current.answerLanguage.length()) return _current.answerLanguage;
  return isEnglishQuestion() ? "en" : "ko";
}

String TutorEngine::subjectName() const {
  if (_subject == Subject::Daily) {
    String label = _currentIsReview ? "REVIEW" : _dailyPhaseLabel;
    if (_current.levelCode.length()) label += " " + _current.levelCode;
    if (_learning) label += " " + String(_learning->remainingSeconds()/60) + ":" + String((_learning->remainingSeconds()%60 + 100)).substring(1);
    return label;
  }
  if (_subject == Subject::English) return "ENGLISH";
  if (_subject == Subject::Math6) return "MATH 6YO";
  if (_subject == Subject::Math) return (_learning && _learning->freeMath6yo()) ? "MATH 6YO" : "MATH";
  return isEnglishQuestion() ? "MIX:EN" : "MIX:MATH";
}

void TutorEngine::buildChoices() {
  _displayChoices = _current.choices;
  if (!_displayChoices.empty()) {
    if (_displayChoices.size() > 4) _displayChoices.resize(4);
    return;
  }
  if (_current.answerType == "number") {
    int v = _current.answer.toInt();
    std::vector<int> nums = {max(0, v-1), v, v+1, v+2};
    std::vector<int> unique;
    for (int n : nums) if (std::find(unique.begin(), unique.end(), n) == unique.end()) unique.push_back(n);
    while (unique.size() < 4) unique.push_back(v + (int)unique.size() + 1);
    for (int i=(int)unique.size()-1;i>0;--i) { int j=random(i+1); std::swap(unique[i],unique[j]); }
    for (int n : unique) _displayChoices.push_back(String(n));
  } else {
    _displayChoices.push_back(_current.answer);
    _displayChoices.push_back("I don't know");
    _displayChoices.push_back("Try again");
  }
}

bool TutorEngine::finishDailyIfNeeded() {
  if (_subject != Subject::Daily || !_learning || !_learning->sessionExpired()) return false;
  _learning->finishDaily(_level, _stars);
  _ui->drawFace(FaceMood::Happy);
  _ui->showMessage("10 MIN DONE!", "Report: /kids_tutor/reports/latest_report.html");
  _ui->nod();
  _ui->speak(_learning->completionSpeech(), "ko");
  delay(3000);
  _sessionComplete = true;
  _voicePending = false;
  return true;
}

bool TutorEngine::loadNext() {
  if (finishDailyIfNeeded()) return false;

  if (_subject == Subject::Daily && _learning) {
    String oldId = _current.id;
    if (!_learning->nextDailyQuestion(_level, oldId, _current, _currentEnglish, _currentIsReview, _dailyPhaseLabel)) {
      if (_learning->sessionExpired()) return !finishDailyIfNeeded();
      return false;
    }
  } else {
    QuestionDB* db = nullptr;
    if (_subject == Subject::English) { db = _english; _currentEnglish = true; }
    else if (_subject == Subject::Math6) { db = _math6; _currentEnglish = false; }
    else if (_subject == Subject::Math) { db = freeMathDb(); _currentEnglish = false; }
    else {
      _currentEnglish = _mixedEnglishNext;
      db = _mixedEnglishNext ? _english : _math;
      _mixedEnglishNext = !_mixedEnglishNext;
    }
    _currentIsReview = false;
    if (!db) return false;
    String oldId = _current.id;
    if (!db->randomByLevel(_level, _current, oldId)) return false;
  }

  _wrong = 0;
  _selected = 0;
  buildChoices();
  render();
  _ui->drawFace(FaceMood::Neutral);
  delay(180);
  render();
  _ui->speakQuestion(_current, languageHint());
  _voicePending = _ui->voiceAutoListen();
  return true;
}

void TutorEngine::render() {
  uint8_t displayLevel = (_subject == Subject::Daily) ? _current.level : _level;
  _shownRemaining = (_subject == Subject::Daily && _learning) ? _learning->remainingSeconds() : 0;
  _ui->showQuestion(_current, _displayChoices, _selected, subjectName(), displayLevel, _stars);
}

void TutorEngine::renderStatus() {
  uint8_t displayLevel = (_subject == Subject::Daily) ? _current.level : _level;
  _ui->showStatusLine(subjectName(), displayLevel, _stars);
}

void TutorEngine::tick() {
  if (_subject != Subject::Daily || !_learning || _grading || _sessionComplete) return;
  if (finishDailyIfNeeded()) return;
  uint32_t remaining = _learning->remainingSeconds();
  if (remaining == _shownRemaining) return;
  _shownRemaining = remaining;
  renderStatus();
}

void TutorEngine::previousChoice() {
  _voicePending = false;
  if (_displayChoices.empty()) return;
  _selected = (_selected - 1 + (int)_displayChoices.size()) % (int)_displayChoices.size();
  render();
}

void TutorEngine::nextChoice() {
  _voicePending = false;
  if (_displayChoices.empty()) return;
  _selected = (_selected + 1) % (int)_displayChoices.size();
  render();
}

void TutorEngine::submitChoice() {
  _voicePending = false;
  if (_displayChoices.empty()) return;
  gradeAnswer(_displayChoices[_selected], false);
}

void TutorEngine::requestVoiceRetry() {
  if (_ui && _ui->voiceReady()) _voicePending = true;
}

void TutorEngine::pollVoice() {
  if (!_voicePending || _grading || !_ui || !_ui->voiceReady()) return;
  _voicePending = false;
  String heard;
  if (!_ui->listen(heard, languageHint())) { render(); return; }
  delay(300);
  gradeAnswer(heard, true);
}

void TutorEngine::gradeAnswer(const String& answer, bool fromVoice) {
  if (_grading) return;
  _grading = true;
  bool correct = fromVoice ? voiceMatches(answer) : answersEqual(answer, _current.answer);
  Serial.printf("[GRADE] source=%s heard='%s' expected='%s' correct=%d review=%d\n",
                fromVoice ? "voice" : "button", answer.c_str(), _current.answer.c_str(), correct, _currentIsReview);

  if (correct) {
    uint8_t attempts = _wrong + 1;
    if (_subject == Subject::Daily && _learning)
      _learning->noteQuestionFinished(_current, _currentEnglish, _currentIsReview, true, attempts, answer);

    _stars += 1;
    _streak += 1;
    _ui->drawFace(FaceMood::Happy);
    _ui->nod();
    String praise = isEnglishQuestion() ? "Great job!" : "정답! 잘했어!";
    _ui->speak(praise, languageHint());
    delay(350);
    if (_subject != Subject::Daily && _streak >= LEVEL_UP_STREAK && _level < currentLevelCap()) {
      _level++;
      _streak = 0;
      _ui->showMessage("LEVEL UP!", "Level " + String(_level));
      _ui->speak(isEnglishQuestion() ? "Level up! Great job!" : "레벨 업! 잘했어!", languageHint());
      delay(450);
    }
    _grading = false;
    loadNext();
    return;
  }

  _streak = 0;
  _wrong++;
  if (_subject == Subject::Daily && _learning && _wrong == 1)
    _learning->noteWrongAttempt(_current, _currentEnglish, answer);

  _ui->drawFace(FaceMood::Thinking);
  _ui->thinkMove();
  String feedback;
  if (_wrong == 1) feedback = _current.hint1;
  else if (_wrong == 2) feedback = _current.hint2;
  else feedback = _current.explanation;
  if (!feedback.length()) feedback = isEnglishQuestion() ? "Try one more time." : "한 번 더 생각해보자.";
  _ui->showMessage(_wrong >= WRONG_BEFORE_EXPLAIN ? "LET'S LEARN" : "HINT", feedback);
  _ui->speak(feedback, languageHint());
  delay(350);

  if (_wrong >= WRONG_BEFORE_EXPLAIN) {
    if (_subject == Subject::Daily && _learning)
      _learning->noteQuestionFinished(_current, _currentEnglish, _currentIsReview, false, _wrong, answer);
    if (_subject != Subject::Daily && _level > 1) _level--;
    _grading = false;
    loadNext();
  } else {
    render();
    _grading = false;
    _voicePending = _ui->voiceAutoListen();
  }
}

void TutorEngine::skip() {
  _voicePending = false;
  _streak = 0;
  if (_subject == Subject::Daily && _learning)
    _learning->noteQuestionFinished(_current, _currentEnglish, _currentIsReview, false, 1, "SKIPPED");
  loadNext();
}
