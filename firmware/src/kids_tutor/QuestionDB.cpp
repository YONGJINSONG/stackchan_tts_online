#include "QuestionDB.h"
#include <ArduinoJson.h>
#include <algorithm>

bool QuestionDB::begin(fs::FS& fs, const char* dataPath, const char* indexPath) {
  _fs = &fs;
  _dataPath = dataPath;
  if (_file) _file.close();
  _offsets.clear();

  if (!loadIndex(indexPath) || !buildMetadata()) {
    Serial.printf("[DB] %s: index unusable, rebuilding from data file\n", dataPath);
    _offsets.clear();
    if (!buildIndex() || !buildMetadata()) return false;
  }
  return !_offsets.empty();
}

uint32_t QuestionDB::hashId(const String& id) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < id.length(); ++i) { h ^= (uint8_t)id[i]; h *= 16777619UL; }
  return h;
}

bool QuestionDB::ensureOpen() {
  if (_file) return true;
  if (!_fs) return false;
  _file = _fs->open(_dataPath, FILE_READ);
  return (bool)_file;
}

bool QuestionDB::loadIndex(const char* indexPath) {
  File f = _fs->open(indexPath, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) _offsets.push_back((uint32_t)strtoul(line.c_str(), nullptr, 10));
  }
  f.close();
  return !_offsets.empty();
}

bool QuestionDB::buildIndex() {
  if (!ensureOpen() || !_file.seek(0)) return false;
  while (_file.available()) {
    uint32_t pos = (uint32_t)_file.position();
    String line = _file.readStringUntil('\n');
    line.trim();
    if (line.length() > 2) _offsets.push_back(pos);
  }
  return !_offsets.empty();
}

bool QuestionDB::buildMetadata() {
  _levels.assign(_offsets.size(), 0);
  _idHashes.assign(_offsets.size(), 0);
  _maxLevel = 1;
  size_t bad = 0;
  for (size_t i = 0; i < _offsets.size(); ++i) {
    Question q;
    if (!readAt(i, q)) { ++bad; continue; }
    _levels[i] = q.level ? q.level : 1;
    _idHashes[i] = hashId(q.id);
    if (_levels[i] > _maxLevel) _maxLevel = _levels[i];
  }
  if (bad) Serial.printf("[DB] %s: %u/%u records unreadable\n",
                         _dataPath.c_str(), (unsigned)bad, (unsigned)_offsets.size());
  return bad * 10 <= _offsets.size();
}

bool QuestionDB::readLineAt(size_t index, String& line) {
  if (index >= _offsets.size()) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!ensureOpen()) return false;
    if (_file.seek(_offsets[index])) {
      line = _file.readStringUntil('\n');
      if (line.length() > 2) return true;
    }
    _file.close();
  }
  return false;
}

bool QuestionDB::parseLine(const String& line, Question& q) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return false;

  q.id = doc["id"] | "";
  q.category = doc["category"] | "";
  q.mode = doc["mode"] | "";
  q.domain = doc["domain"] | "";
  if (doc["level"].is<int>()) q.level = doc["level"].as<int>();
  else q.level = doc["level_num"] | 1;
  q.levelCode = doc["level_code"] | "";
  if (!q.levelCode.length()) q.levelCode = String(q.level);
  q.difficulty = doc["difficulty"] | 1;
  q.question = doc["question"] | "";
  q.tts = doc["tts"] | "";
  if (!q.tts.length()) q.tts = q.question;
  q.answerType = doc["answer_type"] | "text";

  if (doc["answer"].is<const char*>()) q.answer = doc["answer"].as<const char*>();
  else if (doc["answer"].is<int>()) q.answer = String(doc["answer"].as<int>());
  else if (doc["answer"].is<float>()) q.answer = String(doc["answer"].as<float>());
  else q.answer = doc["answer"].as<String>();

  q.choices.clear();
  if (doc["choices"].is<JsonArray>()) {
    for (JsonVariant v : doc["choices"].as<JsonArray>()) {
      if (v.is<const char*>()) q.choices.push_back(v.as<const char*>());
      else if (v.is<int>()) q.choices.push_back(String(v.as<int>()));
      else q.choices.push_back(v.as<String>());
    }
  }

  q.hint1 = doc["hint1"] | "";
  q.hint2 = doc["hint2"] | "";
  q.explanation = doc["explanation"] | "";
  q.speechAnswers.clear();
  if (doc["speech_answer"].is<JsonArray>()) {
    for (JsonVariant v : doc["speech_answer"].as<JsonArray>()) q.speechAnswers.push_back(v.as<String>());
  }
  if (q.speechAnswers.empty()) q.speechAnswers.push_back(q.answer);
  q.answerLanguage = doc["answer_language"] | "";
  q.image = doc["image"] | "";

  q.visualType = doc["visual_type"] | "";
  q.visualData = doc["visual_data"] | "";
  q.visualChoices.clear();
  if (doc["visual_choices"].is<JsonArray>()) {
    for (JsonVariant v : doc["visual_choices"].as<JsonArray>()) q.visualChoices.push_back(v.as<String>());
  }

  return q.id.length() > 0;
}

bool QuestionDB::readAt(size_t index, Question& out) {
  String line;
  return readLineAt(index, line) && parseLine(line, out);
}

uint8_t QuestionDB::clampLevel(uint8_t level) const {
  if (level < 1) return 1;
  return level > _maxLevel ? _maxLevel : level;
}

bool QuestionDB::pickRandomAtLevel(uint8_t level, uint32_t avoidHash, size_t& index) const {
  uint32_t seen = 0;
  for (size_t i = 0; i < _levels.size(); ++i) {
    if (_levels[i] != level) continue;
    if (avoidHash && _idHashes[i] == avoidHash) continue;
    if (random((long)(++seen)) == 0) index = i;
  }
  return seen > 0;
}

bool QuestionDB::randomByLevel(uint8_t level, Question& out, const String& avoidId) {
  if (_offsets.empty()) return false;
  const uint32_t avoidHash = avoidId.length() ? hashId(avoidId) : 0;
  for (int lv = clampLevel(level); lv >= 1; --lv) {
    size_t index = 0;
    if (pickRandomAtLevel((uint8_t)lv, avoidHash, index) && readAt(index, out)) return true;
  }
  const size_t n = _offsets.size();
  const size_t start = (size_t)random((long)n);
  for (size_t k = 0; k < n; ++k) {
    const size_t i = (start + k) % n;
    if (_levels[i] && readAt(i, out)) return true;
  }
  return false;
}

bool QuestionDB::findById(const String& id, Question& out) {
  if (id.length() == 0) return false;
  const uint32_t h = hashId(id);
  for (size_t i = 0; i < _idHashes.size(); ++i) {
    if (_idHashes[i] != h) continue;
    Question q;
    if (readAt(i, q) && q.id == id) { out = q; return true; }
  }
  return false;
}
