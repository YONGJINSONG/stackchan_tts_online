#include "LearningManager.h"
#include "TutorConfig.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <M5Unified.h>
#include <time.h>
#include <algorithm>
#include <math.h>

static const uint32_t REVIEW_INTERVAL_DAYS[] = {0, 1, 3, 7, 14};

namespace {
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y-399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint32_t rtcEpochKst() {
  if (!M5.Rtc.isEnabled()) return 0;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024 || dt.date.year > 2099 || dt.date.month < 1 || dt.date.month > 12 || dt.date.date < 1 || dt.date.date > 31) return 0;
  int64_t sec = daysFromCivil(dt.date.year, dt.date.month, dt.date.date) * 86400LL
              + dt.time.hours * 3600LL + dt.time.minutes * 60LL + dt.time.seconds;
  // RTC is kept in Korea local time in this project. Convert to UTC epoch so
  // it remains compatible with NTP-derived Unix epoch values.
  sec -= 9LL * 3600LL;
  return sec > 0 ? (uint32_t)sec : 0;
}

bool rtcDateTimeValid(m5::rtc_datetime_t& dt) {
  if (!M5.Rtc.isEnabled()) return false;
  dt = M5.Rtc.getDateTime();
  return dt.date.year >= 2024 && dt.date.year <= 2099 && dt.date.month >= 1 && dt.date.month <= 12 && dt.date.date >= 1 && dt.date.date <= 31;
}

int monthFromAbbrev(const char* mon) {
  static const char* months="JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* p=strstr(months,mon); return p ? (int)((p-months)/3)+1 : 1;
}

void seedRtcFromBuildTimeIfNeeded() {
  if (!M5.Rtc.isEnabled()) return;
  m5::rtc_datetime_t cur;
  if (rtcDateTimeValid(cur)) return;
  char mon[4]={0}; int day=1,year=2026,h=0,m=0,sec=0;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &h, &m, &sec);
  int month=monthFromAbbrev(mon); int64_t days=daysFromCivil(year,month,day); int wd=(int)((days+4)%7); if(wd<0)wd+=7;
  m5::rtc_date_t date(year, month, day, wd);
  m5::rtc_time_t time(h,m,sec);
  M5.Rtc.setDateTime(m5::rtc_datetime_t(date,time));
  Serial.println("[RTC] Invalid/empty RTC seeded from firmware build time. Set/sync RTC once for exact scheduling.");
}
}

bool LearningManager::begin(fs::FS& fs, QuestionDB& englishDb, QuestionDB& mathDb, QuestionDB& curriculumDb) {
  _fs = &fs;
  _english = &englishDb;
  _math = &mathDb;
  _curriculum = &curriculumDb;
  _adaptive.begin(fs, curriculumDb);
  loadStudentConfig();
  loadCurriculumConfig();

  seedRtcFromBuildTimeIfNeeded();

  // Network time is optional. If Wi-Fi happens to be connected (for optional
  // cloud STT), use NTP and also refresh the hardware RTC. Offline operation
  // continues to use M5.Rtc without any server.
  if (WiFi.status() == WL_CONNECTED) {
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    uint32_t start = millis();
    while (time(nullptr) < 1700000000 && millis() - start < 2500) delay(80);
    time_t t=time(nullptr);
    if (t >= 1700000000 && M5.Rtc.isEnabled()) {
      struct tm local{}; localtime_r(&t,&local); M5.Rtc.setDateTime(&local);
    }
  }
  return loadReviewQueue();
}

bool LearningManager::loadStudentConfig() {
  File f = _fs->open(STUDENT_CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  _studentName = doc["name"] | _studentName;
  _studentAsciiName = doc["display_name_ascii"] | _studentAsciiName;
  _studentAge = doc["age"] | _studentAge;
  _maxLevel = doc["max_level"] | _maxLevel;
  if (_maxLevel < 1) _maxLevel = 1;
  if (_maxLevel > MAX_LEVEL) _maxLevel = MAX_LEVEL;
  _startLevelConfig = doc["start_level"] | _startLevelConfig;
  if (_startLevelConfig < 1) _startLevelConfig = 1;
  if (_startLevelConfig > _maxLevel) _startLevelConfig = _maxLevel;
  String mathPack = doc["free_math"] | "classic";
  _freeMath6yo = mathPack == "6yo";
  return true;
}

bool LearningManager::loadCurriculumConfig() {
  File f = _fs->open(CURRICULUM_CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  _dailySeconds = (uint32_t)(doc["daily_minutes"] | 10) * 60UL;
  _warmupSeconds = doc["warmup_review_seconds"] | 60;
  _englishSeconds = doc["english_seconds"] | 180;
  _somaSeconds = doc["soma_seconds"] | 180;
  _factoSeconds = doc["facto_seconds"] | 120;
  _finalReviewSeconds = doc["final_review_seconds"] | 60;
  uint32_t sum = _warmupSeconds + _englishSeconds + _somaSeconds + _factoSeconds + _finalReviewSeconds;
  if (sum != _dailySeconds) _dailySeconds = sum;
  return true;
}

uint32_t LearningManager::nowEpoch() const {
  time_t t = time(nullptr);
  if (t > 1700000000) return (uint32_t)t;
  return rtcEpochKst();
}

String LearningManager::dateKey() const {
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm local{}; localtime_r(&t, &local); char buf[16]; strftime(buf, sizeof(buf), "%Y-%m-%d", &local); return String(buf);
  }
  m5::rtc_datetime_t dt;
  if (rtcDateTimeValid(dt)) {
    char buf[16]; snprintf(buf,sizeof(buf),"%04d-%02d-%02d",dt.date.year,dt.date.month,dt.date.date); return String(buf);
  }
  return "session-" + String(millis() / 1000UL);
}

String LearningManager::timestampText() const {
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm local{}; localtime_r(&t, &local); char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S KST", &local); return String(buf);
  }
  m5::rtc_datetime_t dt;
  if (rtcDateTimeValid(dt)) {
    char buf[32]; snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d KST",dt.date.year,dt.date.month,dt.date.date,dt.time.hours,dt.time.minutes,dt.time.seconds); return String(buf);
  }
  return "clock-unavailable";
}

bool LearningManager::loadReviewQueue() {
  _review.clear();
  File f = _fs->open(REVIEW_QUEUE_PATH, FILE_READ);
  if (!f) return true; // first run
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  JsonArray arr = doc["items"].as<JsonArray>();
  for (JsonObject o : arr) {
    ReviewItem r;
    r.id = o["id"] | "";
    r.english = o["english"] | true;
    r.stage = o["stage"] | 0;
    r.lapses = o["lapses"] | 0;
    r.dueEpoch = o["due_epoch"] | 0;
    if (r.id.length()) _review.push_back(r);
  }
  return true;
}

bool LearningManager::saveReviewQueue() {
  if (!_fs) return false;
  _fs->mkdir("/kids_tutor/progress");
  _fs->remove(REVIEW_QUEUE_PATH);
  File f = _fs->open(REVIEW_QUEUE_PATH, FILE_WRITE);
  if (!f) return false;
  JsonDocument doc;
  JsonArray arr = doc["items"].to<JsonArray>();
  for (const auto& r : _review) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = r.id;
    o["english"] = r.english;
    o["stage"] = r.stage;
    o["lapses"] = r.lapses;
    o["due_epoch"] = r.dueEpoch;
  }
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

LearningManager::ReviewItem* LearningManager::findReview(const String& id) {
  for (auto& r : _review) if (r.id == id) return &r;
  return nullptr;
}

void LearningManager::addSessionMistake(const String& id) {
  for (const auto& x : _sessionMistakes) if (x == id) return;
  _sessionMistakes.push_back(id);
}

void LearningManager::removeSessionMistake(const String& id) {
  _sessionMistakes.erase(std::remove(_sessionMistakes.begin(), _sessionMistakes.end(), id), _sessionMistakes.end());
}

void LearningManager::recordMistake(const Question& q, bool isEnglish) {
  addSessionMistake(q.id);
  ReviewItem* r = findReview(q.id);
  if (!r) {
    ReviewItem item;
    item.id = q.id;
    item.english = isEnglish;
    item.stage = 0;
    item.lapses = 1;
    item.dueEpoch = nowEpoch();
    _review.push_back(item);
  } else {
    r->english = isEnglish;
    r->lapses++;
    r->stage = 0;
    r->dueEpoch = nowEpoch();
  }
  saveReviewQueue();
}

void LearningManager::recordReviewCorrect(const String& id) {
  ReviewItem* r = findReview(id);
  if (!r) return;
  if (r->stage < 4) r->stage++;
  uint32_t now = nowEpoch();
  uint32_t days = REVIEW_INTERVAL_DAYS[r->stage];
  r->dueEpoch = now ? now + days * 86400UL : 0;
  removeSessionMistake(id);
  saveReviewQueue();
}

LearningManager::DomainStat& LearningManager::domainStat(const String& key) {
  for (auto& s : _domainStats) if (s.key == key) return s;
  DomainStat s; s.key = key; _domainStats.push_back(s);
  return _domainStats.back();
}

bool LearningManager::startDaily(uint8_t startLevel) {
  _sessionStartMs = millis();
  _sessionStartEpoch = nowEpoch();
  _startLevel = startLevel;
  _startCodeEnglish = _adaptive.currentLevelCode("english");
  _startCodeSoma = _adaptive.currentLevelCode("math_somamath");
  _startCodeFacto = _adaptive.currentLevelCode("math_facto");
  _dailyActive = true;
  _finished = false;
  _questionSerial = 0;
  _answered = _firstTryCorrect = _finalCorrect = 0;
  _englishAnswered = _englishFirstTry = _mathAnswered = _mathFirstTry = 0;
  _reviewAnswered = 0;
  _sessionMistakes.clear();
  _domainStats.clear();
  _mistakeLogs.clear();
  _latestReportPath = "";
  _adaptive.startSession();
  return true;
}

String LearningManager::introSpeech() const {
  return _studentName + "야, A 버튼은 오늘의 10분 공부야. 영어만 하려면 B, 수학만 하려면 C 버튼을 눌러줘.";
}

String LearningManager::greeting() const {
  return _studentName + "야, 오늘도 10분만 영어와 수학을 같이 해보자!";
}

uint32_t LearningManager::remainingSeconds() const {
  if (!_dailyActive) return 0;
  uint32_t elapsed = (millis() - _sessionStartMs) / 1000UL;
  return elapsed >= _dailySeconds ? 0 : _dailySeconds - elapsed;
}

bool LearningManager::sessionExpired() const {
  return _dailyActive && ((millis() - _sessionStartMs) / 1000UL >= _dailySeconds);
}

String LearningManager::currentPhaseLabel() const {
  if (!_dailyActive) return "DAILY";
  // Preserve the old 1:3:3:2:1 daily balance, but drive it by question count
  // so a child who answers quickly still reaches every learning track.
  if (_questionSerial <= 1) return "REVIEW";
  if (_questionSerial <= 4) return "ENGLISH";
  if (_questionSerial <= 7) return "SOMA";
  if (_questionSerial <= 9) return "FACTO";
  return "REVIEW";
}

bool LearningManager::chooseAdaptiveQuestion(const String& category, const String& avoidId,
                                             Question& out, bool& isReview) {
  return _adaptive.pickTrack(category, avoidId, nowEpoch(), out, isReview);
}

bool LearningManager::loadReviewQuestion(Question& out, bool& isEnglish, bool preferSessionMistake) {
  uint32_t now = nowEpoch();
  if (preferSessionMistake && !_sessionMistakes.empty()) {
    for (size_t i = 0; i < _sessionMistakes.size(); ++i) {
      ReviewItem* r = findReview(_sessionMistakes[i]);
      if (!r) continue;
      QuestionDB* db = r->english ? _english : _math;
      if (db->findById(r->id, out)) { isEnglish = r->english; return true; }
    }
  }
  for (auto& r : _review) {
    bool due = (r.dueEpoch == 0 || now == 0 || r.dueEpoch <= now);
    if (!due) continue;
    QuestionDB* db = r.english ? _english : _math;
    if (db->findById(r.id, out)) { isEnglish = r.english; return true; }
  }
  return false;
}

bool LearningManager::nextDailyQuestion(uint8_t level, const String& avoidId, Question& out,
                                        bool& isEnglish, bool& isReview, String& phaseLabel) {
  (void)level;  // Daily mode uses independent EN/K/P adaptive levels.
  if (!_dailyActive || sessionExpired()) return false;
  _questionSerial++;
  phaseLabel = currentPhaseLabel();
  isReview = false;

  const uint32_t questionNumber = _questionSerial;
  String category;

  if (questionNumber == 1) {
    if (_adaptive.pickDueReview(nowEpoch(), false, out, category)) {
      isReview = true;
      isEnglish = (category == "english");
      return true;
    }
    // Backward compatibility: finish any due review queued by v4.4.
    if (loadReviewQuestion(out, isEnglish, false)) {
      isReview = true;
      return true;
    }
    category = "english";
    isEnglish = true;
    return chooseAdaptiveQuestion(category, avoidId, out, isReview);
  }

  if (questionNumber <= 4) {
    category = "english";
    isEnglish = true;
    return chooseAdaptiveQuestion(category, avoidId, out, isReview);
  }

  if (questionNumber <= 7) {
    category = "math_somamath";
    isEnglish = false;
    return chooseAdaptiveQuestion(category, avoidId, out, isReview);
  }

  if (questionNumber <= 9) {
    category = "math_facto";
    isEnglish = false;
    return chooseAdaptiveQuestion(category, avoidId, out, isReview);
  }

  // Question 10: prioritize box-1 items made during this session, then any due review.
  if (_adaptive.pickDueReview(nowEpoch(), true, out, category) ||
      _adaptive.pickDueReview(nowEpoch(), false, out, category)) {
    isReview = true;
    isEnglish = (category == "english");
    return true;
  }
  if (loadReviewQuestion(out, isEnglish, true)) {
    isReview = true;
    return true;
  }

  // Nothing due: rotate across all three tracks.
  int slot = _questionSerial % 3;
  category = slot == 0 ? "english" : (slot == 1 ? "math_somamath" : "math_facto");
  isEnglish = (category == "english");
  return chooseAdaptiveQuestion(category, avoidId, out, isReview);
}

void LearningManager::noteWrongAttempt(const Question& q, bool isEnglish, const String& heard) {
  (void)heard;
  // Adaptive v4.5 waits until the question is finished so assisted-correct
  // answers can still be scheduled as box-1 review without double-counting.
  if (q.category.length()) {
    addSessionMistake(q.id);
    return;
  }
  recordMistake(q, isEnglish);
}

void LearningManager::noteQuestionFinished(const Question& q, bool isEnglish, bool isReview,
                                           bool finalCorrect, uint8_t attempts, const String& heard) {
  if (q.category.length()) {
    bool firstTry = finalCorrect && attempts == 1;
    _adaptive.recordAnswer(q, firstTry, finalCorrect, isReview, nowEpoch());
    if (firstTry) removeSessionMistake(q.id);
  }

  if (isReview) {
    _reviewAnswered++;
    if (!q.category.length()) {
      if (finalCorrect) recordReviewCorrect(q.id);
      else recordMistake(q, isEnglish);
    }
  } else {
    _answered++;
    if (finalCorrect) _finalCorrect++;
    if (attempts == 1 && finalCorrect) _firstTryCorrect++;
    if (isEnglish) {
      _englishAnswered++;
      if (attempts == 1 && finalCorrect) _englishFirstTry++;
    } else {
      _mathAnswered++;
      if (attempts == 1 && finalCorrect) _mathFirstTry++;
    }
    DomainStat& st = domainStat((isEnglish ? "EN:" : "MATH:") + q.domain);
    st.total++;
    st.attempts += attempts;
    if (attempts == 1 && finalCorrect) st.firstTry++;
    if (finalCorrect) st.finalCorrect++;
  }

  if (attempts > 1 || !finalCorrect) {
    MistakeLog m;
    m.id = q.id;
    m.subject = isEnglish ? "English" : "Math";
    m.domain = q.domain;
    m.question = q.question;
    m.answer = q.answer;
    m.heard = heard;
    m.attempts = attempts;
    if (_mistakeLogs.size() < 30) _mistakeLogs.push_back(m);
  }
}

String LearningManager::weakestDomain() const {
  const DomainStat* weak = nullptr;
  float weakRate = 2.0f;
  for (const auto& s : _domainStats) {
    if (s.total == 0) continue;
    float rate = s.firstTry / (float)s.total;
    if (rate < weakRate) { weakRate = rate; weak = &s; }
  }
  return weak ? weak->key : "없음";
}

String LearningManager::recommendationText() const {
  String weak = weakestDomain();
  if (weak == "없음") return "오늘 학습을 안정적으로 마쳤습니다. 내일도 10분 루틴을 이어가세요.";
  if (weak.startsWith("EN:")) return "영어에서 '" + weak.substring(3) + "' 유형을 내일 첫 복습에 우선 배치합니다.";
  return "수학에서 '" + weak.substring(5) + "' 유형을 내일 첫 복습에 우선 배치합니다.";
}

bool LearningManager::writeReports(uint8_t endLevel, uint32_t stars) {
  if (!_fs) return false;
  _fs->mkdir("/kids_tutor/reports");
  String key = dateKey();
  String base = String(REPORTS_DIR) + "/" + key;
  String jsonPath = base + ".json";
  String htmlPath = base + ".html";
  _latestReportPath = String(REPORTS_DIR) + "/latest_report.html";

  uint32_t duration = (millis() - _sessionStartMs) / 1000UL;
  float firstRate = _answered ? (100.0f * _firstTryCorrect / _answered) : 0;
  float enRate = _englishAnswered ? (100.0f * _englishFirstTry / _englishAnswered) : 0;
  float mathRate = _mathAnswered ? (100.0f * _mathFirstTry / _mathAnswered) : 0;

  _fs->remove(jsonPath);
  File jf = _fs->open(jsonPath, FILE_WRITE);
  if (jf) {
    JsonDocument d;
    d["student"]["name"] = _studentName;
    d["student"]["age"] = _studentAge;
    d["date"] = key;
    d["started_at"] = _sessionStartEpoch;
    d["duration_seconds"] = duration;
    d["questions"] = _answered;
    d["first_try_correct"] = _firstTryCorrect;
    d["first_try_accuracy"] = firstRate;
    d["final_correct"] = _finalCorrect;
    d["review_questions"] = _reviewAnswered;
    d["english"]["questions"] = _englishAnswered;
    d["english"]["first_try_accuracy"] = enRate;
    d["math"]["questions"] = _mathAnswered;
    d["math"]["first_try_accuracy"] = mathRate;
    d["level"]["start"] = _startLevel;
    d["level"]["end"] = endLevel;
    d["adaptive_levels"]["english"] = _adaptive.currentLevelCode("english");
    d["adaptive_levels"]["soma"] = _adaptive.currentLevelCode("math_somamath");
    d["adaptive_levels"]["facto"] = _adaptive.currentLevelCode("math_facto");
    d["adaptive_levels_start"]["english"] = _startCodeEnglish;
    d["adaptive_levels_start"]["soma"] = _startCodeSoma;
    d["adaptive_levels_start"]["facto"] = _startCodeFacto;
    d["stars"] = stars;
    d["weakest_domain"] = weakestDomain();
    d["recommendation"] = recommendationText();
    JsonArray wa = d["mistakes"].to<JsonArray>();
    for (const auto& m : _mistakeLogs) {
      JsonObject o = wa.add<JsonObject>();
      o["id"] = m.id; o["subject"] = m.subject; o["domain"] = m.domain;
      o["question"] = m.question; o["answer"] = m.answer; o["heard"] = m.heard; o["attempts"] = m.attempts;
    }
    serializeJsonPretty(d, jf);
    jf.close();
  }

  // Append compact history CSV for long-term parent tracking.
  bool needHeader = !_fs->exists(HISTORY_CSV_PATH);
  File hf = _fs->open(HISTORY_CSV_PATH, FILE_APPEND);
  if (hf) {
    if (needHeader) hf.println("date,duration_sec,questions,first_try_accuracy,english_accuracy,math_accuracy,reviews,start_level,end_level,stars,weakest_domain");
    hf.printf("%s,%u,%u,%.1f,%.1f,%.1f,%u,%u,%u,%u,%s\n",
      key.c_str(), (unsigned)duration, (unsigned)_answered, firstRate, enRate, mathRate,
      (unsigned)_reviewAnswered, (unsigned)_startLevel, (unsigned)endLevel, (unsigned)stars, weakestDomain().c_str());
    hf.close();
  }

  auto writeHtml = [&](const String& path) {
    _fs->remove(path);
    File f = _fs->open(path, FILE_WRITE);
    if (!f) return;
    f.print("<!doctype html><html lang='ko'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    f.print("<title>지우 로니 학습 리포트</title><style>body{font-family:system-ui,sans-serif;max-width:760px;margin:32px auto;padding:0 18px;line-height:1.55} .cards{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}.card{border:1px solid #ddd;border-radius:14px;padding:16px} .big{font-size:28px;font-weight:700} table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #ddd;text-align:left}.muted{color:#666}@media(max-width:560px){.cards{grid-template-columns:1fr}}</style></head><body>");
    f.printf("<h1>%s의 오늘 학습 리포트</h1><p class='muted'>%s · %u세 · %s</p>", _studentName.c_str(), key.c_str(), _studentAge, timestampText().c_str());
    f.print("<div class='cards'>");
    f.printf("<div class='card'><div>학습시간</div><div class='big'>%u분 %u초</div></div>", duration/60, duration%60);
    f.printf("<div class='card'><div>문제 수</div><div class='big'>%u</div></div>", _answered);
    f.printf("<div class='card'><div>첫 시도 정답률</div><div class='big'>%.0f%%</div></div>", firstRate);
    f.printf("<div class='card'><div>자동 복습</div><div class='big'>%u문제</div></div>", _reviewAnswered);
    f.print("</div><h2>영어·수학</h2><table><tr><th>과목</th><th>문제</th><th>첫 시도 정답률</th></tr>");
    f.printf("<tr><td>영어</td><td>%u</td><td>%.0f%%</td></tr>", _englishAnswered, enRate);
    f.printf("<tr><td>수학</td><td>%u</td><td>%.0f%%</td></tr></table>", _mathAnswered, mathRate);
    auto levelRow = [&](const char* label, const String& from, const String& to) {
      if (from == to) f.printf("<tr><td>%s</td><td>%s</td><td class='muted'>유지</td></tr>", label, to.c_str());
      else f.printf("<tr><td>%s</td><td>%s</td><td><b>%s → %s</b></td></tr>", label, to.c_str(), from.c_str(), to.c_str());
    };
    f.print("<h2>적응형 레벨</h2><table><tr><th>트랙</th><th>현재</th><th>오늘 변화</th></tr>");
    levelRow("영어", _startCodeEnglish, _adaptive.currentLevelCode("english"));
    levelRow("소마셈", _startCodeSoma, _adaptive.currentLevelCode("math_somamath"));
    levelRow("팩토", _startCodeFacto, _adaptive.currentLevelCode("math_facto"));
    f.print("</table>");
    f.printf("<h2>오늘의 포인트</h2><p><b>보완 영역:</b> %s</p><p>%s</p>", weakestDomain().c_str(), recommendationText().c_str());
    f.print("<h2>다시 본 문제</h2>");
    if (_mistakeLogs.empty()) f.print("<p>오늘은 별도 오답 기록이 없습니다.</p>");
    else {
      f.print("<table><tr><th>과목/영역</th><th>문제</th><th>정답</th><th>시도</th></tr>");
      for (const auto& m : _mistakeLogs) {
        f.printf("<tr><td>%s / %s</td><td>%s</td><td>%s</td><td>%u</td></tr>", m.subject.c_str(), m.domain.c_str(), m.question.c_str(), m.answer.c_str(), m.attempts);
      }
      f.print("</table>");
    }
    f.print("<p class='muted'>오답 문제는 오늘 세션 마지막 또는 다음 학습일에 자동으로 다시 나오며, 정답 시 1→3→7→14일 간격으로 복습됩니다.</p></body></html>");
    f.close();
  };
  writeHtml(htmlPath);
  writeHtml(_latestReportPath);
  return true;
}

bool LearningManager::finishDaily(uint8_t endLevel, uint32_t stars) {
  if (!_dailyActive || _finished) return false;
  _dailyActive = false;
  _finished = true;
  _adaptive.flush();
  saveReviewQueue();
  return writeReports(endLevel, stars);
}

String LearningManager::completionSpeech() const {
  // Keep the spoken phrase fixed so it can be pre-generated as a local WAV.
  // Detailed counts/accuracy remain available in the parent report.
  return _studentName + "야, 오늘 10분 공부 끝! 잘했어!";
}
