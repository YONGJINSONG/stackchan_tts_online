#include "AdaptiveCurriculum.h"
#include "TutorConfig.h"
#include <ArduinoJson.h>
#include <algorithm>

namespace {
constexpr uint8_t ADAPT_MAX_WINDOW = 30;
constexpr uint8_t ADAPT_MAX_RECENT_GUARD = 12;
}

bool AdaptiveCurriculum::begin(fs::FS& fs, QuestionDB& db) {
  _fs = &fs;
  _db = &db;
  _tracks.clear();

  TrackState en; en.category="english"; en.currentLevel=1; en.maxLevel=3; _tracks.push_back(en);
  TrackState sm; sm.category="math_somamath"; sm.currentLevel=1; sm.maxLevel=8; _tracks.push_back(sm);
  TrackState fa; fa.category="math_facto"; fa.currentLevel=1; fa.maxLevel=6; _tracks.push_back(fa);

  loadPolicyConfig();
  buildPools();
  loadState();
  return true;
}

bool AdaptiveCurriculum::loadPolicyConfig() {
  if (!_fs || !_fs->exists(CURRICULUM_CONFIG_PATH)) return true;
  File f = _fs->open(CURRICULUM_CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return false; }
  f.close();
  JsonObject p = d["adaptive_policy"].as<JsonObject>();
  if (p.isNull()) return true;

  int w = p["fresh_window"] | (int)_window;
  if (w < 4) w = 4; if (w > ADAPT_MAX_WINDOW) w = ADAPT_MAX_WINDOW;
  _window = (uint8_t)w;

  float promote = p["promote_accuracy"] | _promoteAccuracy;
  float demote = p["demote_accuracy"] | _demoteAccuracy;
  if (promote < 0.50f) promote = 0.50f; if (promote > 1.0f) promote = 1.0f;
  if (demote < 0.0f) demote = 0.0f; if (demote > 0.80f) demote = 0.80f;
  if (demote >= promote) demote = promote - 0.10f;
  _promoteAccuracy = promote;
  _demoteAccuracy = demote;

  int minUnique = p["min_unique_for_promotion"] | (int)_minUniqueForPromotion;
  if (minUnique < 4) minUnique = 4; if (minUnique > 20) minUnique = 20;
  _minUniqueForPromotion = (uint8_t)minUnique;

  int gap = p["wrong_review_after_questions"] | (int)_sessionGap;
  if (gap < 1) gap = 1; if (gap > 8) gap = 8;
  _sessionGap = (uint8_t)gap;

  float reviewProb = p["review_probability_in_track"] | (_reviewProbability / 100.0f);
  if (reviewProb < 0.0f) reviewProb = 0.0f; if (reviewProb > 1.0f) reviewProb = 1.0f;
  _reviewProbability = (uint8_t)(reviewProb * 100.0f + 0.5f);

  int rg = p["recent_guard"] | (int)_recentGuard;
  if (rg < 1) rg = 1; if (rg > ADAPT_MAX_RECENT_GUARD) rg = ADAPT_MAX_RECENT_GUARD;
  _recentGuard = (uint8_t)rg;

  int se = p["save_every_questions"] | (int)_saveEvery;
  if (se < 1) se = 1; if (se > 20) se = 20;
  _saveEvery = (uint8_t)se;

  JsonArray days = p["review_intervals_days"].as<JsonArray>();
  if (!days.isNull() && days.size() >= 4) {
    uint16_t prev = 0;
    for (int i=0;i<4;++i) {
      int v = days[i].as<int>();
      if (v < 1) v = 1;
      if (v < prev) v = prev;
      if (v > 365) v = 365;
      _reviewDays[i] = (uint16_t)v;
      prev = _reviewDays[i];
    }
  }
  return true;
}

void AdaptiveCurriculum::startSession() {
  _sessionSerial = 0;
  _recentIds.clear();
  // Box-1 items left from the previous session must be eligible in warm-up.
  for (auto& p : _problems) if (p.box == 1) p.dueQuestion = 0;
}

AdaptiveCurriculum::TrackState* AdaptiveCurriculum::track(const String& category) {
  for (auto& t : _tracks) if (t.category == category) return &t;
  return nullptr;
}
const AdaptiveCurriculum::TrackState* AdaptiveCurriculum::track(const String& category) const {
  for (const auto& t : _tracks) if (t.category == category) return &t;
  return nullptr;
}

AdaptiveCurriculum::LevelPool* AdaptiveCurriculum::levelPool(const String& category, uint8_t level) {
  for (auto& p : _pools) if (p.category == category && p.level == level) return &p;
  return nullptr;
}
const AdaptiveCurriculum::LevelPool* AdaptiveCurriculum::levelPool(const String& category, uint8_t level) const {
  for (const auto& p : _pools) if (p.category == category && p.level == level) return &p;
  return nullptr;
}
void AdaptiveCurriculum::buildPools() {
  _pools.clear();
  if (!_db) return;
  for (size_t i=0;i<_db->size();++i) {
    Question q;
    if (!_db->readAt(i,q) || !q.category.length()) continue;
    LevelPool* p = levelPool(q.category,q.level);
    if (!p) {
      LevelPool np; np.category=q.category; np.level=q.level; _pools.push_back(np);
      p=&_pools.back();
    }
    p->indices.push_back((uint16_t)i);
  }
}
AdaptiveCurriculum::ProblemState* AdaptiveCurriculum::problem(const String& id) {
  for (auto& p : _problems) if (p.id == id) return &p;
  return nullptr;
}
const AdaptiveCurriculum::ProblemState* AdaptiveCurriculum::problem(const String& id) const {
  for (const auto& p : _problems) if (p.id == id) return &p;
  return nullptr;
}
bool AdaptiveCurriculum::isRecent(const String& id) const {
  size_t start = _recentIds.size() > _recentGuard ? _recentIds.size()-_recentGuard : 0;
  for (size_t i=start;i<_recentIds.size();++i) if (_recentIds[i] == id) return true;
  return false;
}

void AdaptiveCurriculum::noteRecent(const String& id) {
  _recentIds.push_back(id);
  if (_recentIds.size() > 16) _recentIds.erase(_recentIds.begin());
}

uint32_t AdaptiveCurriculum::intervalDays(uint8_t box) const {
  switch (box) {
    case 2: return _reviewDays[0];
    case 3: return _reviewDays[1];
    case 4: return _reviewDays[2];
    case 5: return _reviewDays[3];
    default: return 0;
  }
}

bool AdaptiveCurriculum::allowedLevel(const ProblemState& p) const {
  const TrackState* t = track(p.category);
  return t && p.level <= t->currentLevel;
}

bool AdaptiveCurriculum::isDue(const ProblemState& p, uint32_t nowEpoch) const {
  if (!allowedLevel(p)) return false;
  if (p.box == 1) {
    if (p.dueQuestion == 0) return true;
    return _sessionSerial >= p.dueQuestion;
  }
  if (p.dueEpoch == 0 || nowEpoch == 0) return true;
  return p.dueEpoch <= nowEpoch;
}

bool AdaptiveCurriculum::pickDueForCategory(const String& category, uint32_t nowEpoch,
                                             Question& out, bool sessionOnly) {
  std::vector<String> candidates;
  std::vector<String> guarded;
  for (const auto& p : _problems) {
    if (p.category != category) continue;
    if (!isDue(p, nowEpoch)) continue;
    if (sessionOnly && !(p.box == 1 && (p.dueQuestion == 0 || _sessionSerial >= p.dueQuestion))) continue;
    candidates.push_back(p.id);
    if (!isRecent(p.id)) guarded.push_back(p.id);
  }
  auto& use = guarded.empty() ? candidates : guarded;
  if (use.empty()) return false;
  const String& id = use[(size_t)random((long)use.size())];
  return _db->findById(id, out);
}

bool AdaptiveCurriculum::pickDueReview(uint32_t nowEpoch, bool preferSessionDue,
                                        Question& out, String& category) {
  const char* cats[] = {"english","math_somamath","math_facto"};
  if (preferSessionDue) {
    for (int pass=0; pass<3; ++pass) {
      int start = random(3);
      for (int k=0;k<3;++k) {
        String cat = cats[(start+k)%3];
        if (pickDueForCategory(cat, nowEpoch, out, true)) { category=cat; return true; }
      }
    }
  }
  int start = random(3);
  for (int k=0;k<3;++k) {
    String cat = cats[(start+k)%3];
    if (pickDueForCategory(cat, nowEpoch, out, false)) { category=cat; return true; }
  }
  return false;
}

bool AdaptiveCurriculum::pickFreshCurrent(const String& category, const String& avoidId, uint32_t nowEpoch, Question& out) {
  TrackState* t = track(category);
  const LevelPool* lp = t ? levelPool(category,t->currentLevel) : nullptr;
  if (!t || !lp) return false;

  size_t chosen = 0;
  uint32_t candidateCount = 0;
  for (uint16_t idx : lp->indices) {
    Question q;
    if (!_db->readAt(idx,q)) continue;
    bool alreadyAssessed=false; for (const auto& id:t->uniqueCurrent) if(id==q.id){alreadyAssessed=true;break;}
    if (q.id == avoidId || alreadyAssessed || isRecent(q.id)) continue;
    const ProblemState* ps = problem(q.id);
    if (ps && isDue(*ps, nowEpoch)) continue;
    ++candidateCount;
    if (random((long)candidateCount) == 0) { out=q; chosen=1; }
  }
  if (chosen) return true;

  // Relax recent guard, but still prefer unseen.
  candidateCount = 0;
  for (uint16_t idx : lp->indices) {
    Question q;
    if (!_db->readAt(idx,q)) continue;
    bool alreadyAssessed=false; for (const auto& id:t->uniqueCurrent) if(id==q.id){alreadyAssessed=true;break;}
    if (q.id == avoidId || alreadyAssessed) continue;
    const ProblemState* ps = problem(q.id);
    if (ps && isDue(*ps, nowEpoch)) continue;
    ++candidateCount;
    if (random((long)candidateCount) == 0) { out=q; chosen=1; }
  }
  return chosen != 0;
}

bool AdaptiveCurriculum::pickLeastMasteredCurrent(const String& category, const String& avoidId, Question& out) {
  TrackState* t = track(category);
  const LevelPool* lp = t ? levelPool(category,t->currentLevel) : nullptr;
  if (!t || !lp) return false;

  bool found=false;
  uint32_t bestScore=0xffffffffUL;
  Question best;
  for (uint16_t idx : lp->indices) {
    Question q;
    if (!_db->readAt(idx,q) || q.id == avoidId) continue;
    const ProblemState* p = problem(q.id);
    uint32_t score = p ? ((uint32_t)p->box * 100000UL + (uint32_t)p->timesShown * 100UL + p->lastQuestion) : 0;
    if (isRecent(q.id)) score += 500000UL;
    if (!found || score < bestScore) { bestScore=score; best=q; found=true; }
  }
  if (found) out=best;
  return found;
}

bool AdaptiveCurriculum::pickTrack(const String& category, const String& avoidId, uint32_t nowEpoch,
                                    Question& out, bool& isReview) {
  isReview=false;
  if (random(100) < _reviewProbability) {
    if (pickDueForCategory(category, nowEpoch, out, false)) { isReview=true; return true; }
  }
  if (pickFreshCurrent(category, avoidId, nowEpoch, out)) return true;

  // If every item in this level has contributed once to the current assessment
  // cycle but the learner is still between promote/demote thresholds, start a
  // new assessment cycle. This prevents a learner from getting permanently
  // stuck after the finite fresh pool is exhausted. Due review items are still
  // excluded from fresh assessment until their review is completed.
  TrackState* t = track(category);
  const LevelPool* lp = t ? levelPool(category, t->currentLevel) : nullptr;
  if (t && lp && !lp->indices.empty() && t->uniqueCurrent.size() >= lp->indices.size()) {
    t->freshResults.clear();
    t->uniqueCurrent.clear();
    if (pickFreshCurrent(category, avoidId, nowEpoch, out)) return true;
  }

  // Any other repeated item is a review and never counts toward promotion.
  if (pickLeastMasteredCurrent(category, avoidId, out)) { isReview = true; return true; }
  return false;
}

void AdaptiveCurriculum::adjustLevel(TrackState& t) {
  if (t.freshResults.size() < _window) return;
  uint16_t correct=0;
  for (auto v : t.freshResults) if (v) ++correct;
  float accuracy = correct / (float)t.freshResults.size();

  if (accuracy >= _promoteAccuracy && t.uniqueCurrent.size() >= _minUniqueForPromotion && t.currentLevel < t.maxLevel) {
    ++t.currentLevel;
    t.freshResults.clear();
    t.uniqueCurrent.clear();
    ++t.levelChanges;
  } else if (accuracy <= _demoteAccuracy && t.currentLevel > 1) {
    --t.currentLevel;
    t.freshResults.clear();
    t.uniqueCurrent.clear();
    ++t.levelChanges;
  }
}

void AdaptiveCurriculum::recordAnswer(const Question& q, bool firstTryCorrect, bool finalCorrect,
                                       bool isReview, uint32_t nowEpoch) {
  TrackState* t = track(q.category);
  if (!t) return;
  ++_sessionSerial;
  ++t->questionCount;

  ProblemState* p = problem(q.id);
  if (!p) {
    ProblemState x;
    x.id=q.id; x.category=q.category; x.level=q.level;
    _problems.push_back(x);
    p=&_problems.back();
  }
  ++p->timesShown;
  p->lastQuestion=t->questionCount;

  if (finalCorrect && firstTryCorrect) {
    if (p->box < 5) ++p->box;
    p->dueQuestion = 0;
    uint32_t days=intervalDays(p->box);
    p->dueEpoch = nowEpoch ? nowEpoch + days*86400UL : 0;
  } else {
    p->box=1;
    ++p->lapses;
    p->dueQuestion=_sessionSerial + _sessionGap;
    p->dueEpoch=nowEpoch;
  }

  noteRecent(q.id);

  if (!isReview && q.level == t->currentLevel) {
    t->freshResults.push_back((finalCorrect && firstTryCorrect) ? 1 : 0);
    if (t->freshResults.size() > _window) t->freshResults.erase(t->freshResults.begin());
    bool exists=false; for (const auto& id:t->uniqueCurrent) if(id==q.id){exists=true;break;}
    if (!exists) t->uniqueCurrent.push_back(q.id);
    adjustLevel(*t);
  }

  if (++_dirty >= _saveEvery) saveState();
}

uint8_t AdaptiveCurriculum::currentLevel(const String& category) const {
  const TrackState* t=track(category); return t ? t->currentLevel : 1;
}
String AdaptiveCurriculum::levelCode(const String& category, uint8_t level) const {
  if (category=="english") return "EN"+String(level);
  if (category=="math_somamath") return "K"+String(level);
  if (category=="math_facto") return "P"+String(level);
  return String(level);
}
String AdaptiveCurriculum::currentLevelCode(const String& category) const {
  return levelCode(category,currentLevel(category));
}

bool AdaptiveCurriculum::loadState() {
  if (!_fs || !_fs->exists(ADAPTIVE_STATE_PATH)) return true;
  File f=_fs->open(ADAPTIVE_STATE_PATH,FILE_READ);
  if(!f) return false;
  JsonDocument d;
  if(deserializeJson(d,f)){f.close();return false;}
  f.close();

  JsonObject tracks=d["tracks"].as<JsonObject>();
  for(auto& t:_tracks){
    JsonObject o=tracks[t.category].as<JsonObject>();
    if(o.isNull()) continue;
    t.currentLevel=o["current_level"]|t.currentLevel;
    if(t.currentLevel<1)t.currentLevel=1; if(t.currentLevel>t.maxLevel)t.currentLevel=t.maxLevel;
    t.questionCount=o["question_count"]|0;
    t.levelChanges=o["level_changes"]|0;
    t.freshResults.clear();
    for(JsonVariant v:o["fresh_results"].as<JsonArray>()) t.freshResults.push_back(v.as<uint8_t>());
    t.uniqueCurrent.clear();
    for(JsonVariant v:o["unique_current"].as<JsonArray>()) t.uniqueCurrent.push_back(v.as<String>());
  }

  _problems.clear();
  for(JsonObject o:d["problems"].as<JsonArray>()){
    ProblemState p;
    p.id=o["id"]|"";
    p.category=o["category"]|"";
    p.level=o["level"]|1;
    p.box=o["box"]|1;
    p.timesShown=o["times_shown"]|0;
    p.lapses=o["lapses"]|0;
    p.dueEpoch=o["due_epoch"]|0;
    p.dueQuestion=o["due_question"]|0;
    p.lastQuestion=o["last_question"]|0;
    if(p.id.length())_problems.push_back(p);
  }
  return true;
}

bool AdaptiveCurriculum::saveState() {
  if(!_fs)return false;
  _fs->mkdir("/kids_tutor/progress");
  _fs->remove(ADAPTIVE_STATE_PATH);
  File f=_fs->open(ADAPTIVE_STATE_PATH,FILE_WRITE);
  if(!f)return false;
  JsonDocument d;
  d["version"]="4.5.1";
  JsonObject tracks=d["tracks"].to<JsonObject>();
  for(const auto&t:_tracks){
    JsonObject o=tracks[t.category].to<JsonObject>();
    o["current_level"]=t.currentLevel;
    o["question_count"]=t.questionCount;
    o["level_changes"]=t.levelChanges;
    JsonArray fr=o["fresh_results"].to<JsonArray>(); for(auto v:t.freshResults)fr.add(v);
    JsonArray uq=o["unique_current"].to<JsonArray>(); for(const auto&id:t.uniqueCurrent)uq.add(id);
  }
  JsonArray ps=d["problems"].to<JsonArray>();
  for(const auto&p:_problems){
    JsonObject o=ps.add<JsonObject>();
    o["id"]=p.id; o["category"]=p.category; o["level"]=p.level; o["box"]=p.box;
    o["times_shown"]=p.timesShown; o["lapses"]=p.lapses;
    o["due_epoch"]=p.dueEpoch; o["due_question"]=p.dueQuestion; o["last_question"]=p.lastQuestion;
  }
  serializeJsonPretty(d,f); f.close(); _dirty=0; return true;
}
void AdaptiveCurriculum::flush(){ if(_dirty) saveState(); }
