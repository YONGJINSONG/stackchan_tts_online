#include "QuestionDB.h"
#include "QidxFormat.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace {

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
  }
  return crc;
}

bool readExact(File& file, void* data, size_t length) {
  return file.read(static_cast<uint8_t*>(data), length) == length;
}

bool writeExact(File& file, const void* data, size_t length) {
  return file.write(static_cast<const uint8_t*>(data), length) == length;
}

String qidxPathFromLegacy(const char* indexPath) {
  String path = indexPath ? indexPath : "";
  if (path.endsWith(".idx")) path.remove(path.length() - 4);
  path += ".qidx";
  return path;
}

}  // namespace

bool QuestionDB::begin(fs::FS& fs, const char* dataPath, const char* indexPath) {
  const uint32_t started = millis();
  _fs = &fs;
  _dataPath = dataPath;
  if (_file) _file.close();
  clearMetadata();

  uint32_t sourceSize = 0;
  uint64_t sourceMtime = 0;
  uint32_t sourceSampleCrc32[tutor_qidx::SAMPLE_COUNT] = {};
  if (!sourceIdentity(sourceSize, sourceMtime, sourceSampleCrc32)) {
    Serial.printf("[DB] %s: cannot read source identity\n", dataPath);
    return false;
  }

  const String qidxPath = qidxPathFromLegacy(indexPath);
  const String candidates[] = {qidxPath, qidxPath + ".bak", qidxPath + ".tmp"};
  String primaryReason = "missing";
  for (size_t i = 0; i < 3; ++i) {
    if (!_fs->exists(candidates[i])) continue;
    String reason;
    if (loadQidx(candidates[i], sourceSize, sourceMtime, sourceSampleCrc32, &reason)) {
      if (i != 0) {
        if (_fs->exists(qidxPath)) _fs->remove(qidxPath);
        if (_fs->rename(candidates[i], qidxPath)) {
          Serial.printf("[DB] %s: recovered %s\n", dataPath, qidxPath.c_str());
        }
      }
      Serial.printf("[DB] %s: qidx hit records=%u load_ms=%u\n", dataPath,
                    (unsigned)_offsets.size(), (unsigned)(millis() - started));
      return true;
    }
    if (i == 0) primaryReason = reason;
  }

  Serial.printf("[DB] %s: qidx %s; scanning metadata\n",
                dataPath, primaryReason.c_str());
  if (!scanMetadata()) return false;
  if (!writeQidx(qidxPath, sourceSize, sourceMtime, sourceSampleCrc32)) {
    Serial.printf("[DB] %s: qidx write failed; RAM metadata active\n", dataPath);
  }
  Serial.printf("[DB] %s: metadata ready records=%u total_ms=%u\n", dataPath,
                (unsigned)_offsets.size(), (unsigned)(millis() - started));
  return true;
}

uint32_t QuestionDB::hashText(const String& text) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < text.length(); ++i) {
    h ^= (uint8_t)text[i];
    h *= 16777619UL;
  }
  return h;
}

void QuestionDB::clearMetadata() {
  _offsets.clear();
  _levels.clear();
  _idHashes.clear();
  _categoryHashes.clear();
  _maxLevel = 1;
}

bool QuestionDB::ensureOpen() {
  if (_file) return true;
  if (!_fs) return false;
  _file = _fs->open(_dataPath, FILE_READ);
  return (bool)_file;
}

bool QuestionDB::sourceIdentity(uint32_t& sourceSize, uint64_t& sourceMtime,
                                uint32_t sourceSampleCrc32[4]) {
  if (!ensureOpen()) return false;
  const size_t rawSize = _file.size();
  if (rawSize == 0 || rawSize > UINT32_MAX) return false;
  sourceSize = (uint32_t)rawSize;
  sourceMtime = (uint64_t)_file.getLastWrite();

  const uint32_t offsets[4] = {
      0,
      sourceSize / 3,
      (uint32_t)(((uint64_t)sourceSize * 2) / 3),
      sourceSize > tutor_qidx::SAMPLE_WINDOW_BYTES
          ? sourceSize - (uint32_t)tutor_qidx::SAMPLE_WINDOW_BYTES
          : 0};
  uint8_t buffer[tutor_qidx::SAMPLE_WINDOW_BYTES];
  for (uint8_t i = 0; i < 4; ++i) {
    const uint32_t offset = offsets[i];
    const uint16_t length = (uint16_t)std::min<size_t>(
        tutor_qidx::SAMPLE_WINDOW_BYTES, sourceSize - offset);
    if (!_file.seek(offset) || _file.read(buffer, length) != length) return false;
    sourceSampleCrc32[i] =
        crc32Update(0xffffffffUL, buffer, length) ^ 0xffffffffUL;
  }
  return _file.seek(0);
}

bool QuestionDB::loadQidx(const String& path, uint32_t sourceSize,
                          uint64_t sourceMtime,
                          const uint32_t sourceSampleCrc32[4],
                          String* reason) {
  auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };

  File file = _fs->open(path, FILE_READ);
  if (!file) return fail("missing");
  tutor_qidx::Header header{};
  if (!readExact(file, &header, sizeof(header))) return fail("short header");
  if (memcmp(header.magic, tutor_qidx::MAGIC, sizeof(header.magic)) != 0 ||
      header.version != tutor_qidx::VERSION ||
      header.headerSize != sizeof(tutor_qidx::Header) ||
      header.entrySize != sizeof(tutor_qidx::Entry) ||
      header.flags != tutor_qidx::FLAGS || header.reserved != 0) {
    return fail("format mismatch");
  }
  if (header.sourceSize != sourceSize || header.sourceMtime != sourceMtime ||
      memcmp(header.sourceSampleCrc32, sourceSampleCrc32,
             sizeof(header.sourceSampleCrc32)) != 0) {
    return fail("source changed");
  }
  if (header.recordCount == 0 || header.recordCount > sourceSize) {
    return fail("invalid record count");
  }
  const uint64_t expectedSize = (uint64_t)header.headerSize +
                                (uint64_t)header.recordCount * header.entrySize;
  if ((uint64_t)file.size() != expectedSize) return fail("length mismatch");

  SpiRamVector<uint32_t> offsets;
  SpiRamVector<uint8_t> levels;
  SpiRamVector<uint32_t> idHashes;
  SpiRamVector<uint32_t> categoryHashes;
  offsets.reserve(header.recordCount);
  levels.reserve(header.recordCount);
  idHashes.reserve(header.recordCount);
  categoryHashes.reserve(header.recordCount);

  uint32_t crc = 0xffffffffUL;
  uint32_t previousOffset = 0;
  uint8_t maxLevel = 1;
  for (uint32_t i = 0; i < header.recordCount; ++i) {
    tutor_qidx::Entry entry{};
    if (!readExact(file, &entry, sizeof(entry))) return fail("short entries");
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
    if (entry.offset >= sourceSize || (i > 0 && entry.offset <= previousOffset) ||
        entry.reserved[0] || entry.reserved[1] || entry.reserved[2]) {
      return fail("invalid entry");
    }
    previousOffset = entry.offset;
    offsets.push_back(entry.offset);
    levels.push_back(entry.level);
    idHashes.push_back(entry.idHash);
    categoryHashes.push_back(entry.categoryHash);
    if (entry.level > maxLevel) maxLevel = entry.level;
  }
  if ((crc ^ 0xffffffffUL) != header.entriesCrc32) return fail("entry CRC mismatch");

  _offsets = std::move(offsets);
  _levels = std::move(levels);
  _idHashes = std::move(idHashes);
  _categoryHashes = std::move(categoryHashes);
  _maxLevel = maxLevel;
  if (reason) *reason = "ok";
  return true;
}

bool QuestionDB::scanMetadata() {
  if (!ensureOpen() || !_file.seek(0)) return false;
  clearMetadata();
  JsonDocument filter;
  filter["id"] = true;
  filter["category"] = true;
  filter["level"] = true;
  filter["level_num"] = true;
  JsonDocument doc;
  String line;
  _maxLevel = 1;
  size_t bad = 0;
  const uint32_t started = millis();
  while (_file.available()) {
    const uint32_t offset = (uint32_t)_file.position();
    line = _file.readStringUntil('\n');
    line.trim();
    if (line.length() <= 2) continue;

    _offsets.push_back(offset);
    doc.clear();
    const DeserializationError err = deserializeJson(
        doc, line, DeserializationOption::Filter(filter));
    String id = err ? String() : doc["id"].as<String>();
    if (err || id.length() == 0) {
      _levels.push_back(0);
      _idHashes.push_back(0);
      _categoryHashes.push_back(0);
      ++bad;
      continue;
    }

    int rawLevel = doc["level"].is<int>() ? doc["level"].as<int>()
                                            : (doc["level_num"] | 1);
    uint8_t level = (uint8_t)rawLevel;
    if (level == 0) level = 1;
    const String category = doc["category"].as<String>();
    _levels.push_back(level);
    _idHashes.push_back(hashText(id));
    _categoryHashes.push_back(category.length() ? hashText(category) : 0);
    if (level > _maxLevel) _maxLevel = level;
  }
  if (bad) Serial.printf("[DB] %s: %u/%u records unreadable\n",
                         _dataPath.c_str(), (unsigned)bad, (unsigned)_offsets.size());
  Serial.printf("[DB] %s: metadata scan records=%u bad=%u scan_ms=%u\n",
                _dataPath.c_str(), (unsigned)_offsets.size(), (unsigned)bad,
                (unsigned)(millis() - started));
  return !_offsets.empty() && bad * 10 <= _offsets.size();
}

bool QuestionDB::writeQidx(const String& path, uint32_t sourceSize,
                           uint64_t sourceMtime,
                           const uint32_t sourceSampleCrc32[4]) {
  if (!_fs || _offsets.empty() || _levels.size() != _offsets.size() ||
      _idHashes.size() != _offsets.size() ||
      _categoryHashes.size() != _offsets.size()) return false;

  uint32_t entriesCrc = 0xffffffffUL;
  for (size_t i = 0; i < _offsets.size(); ++i) {
    tutor_qidx::Entry entry{};
    entry.offset = _offsets[i];
    entry.idHash = _idHashes[i];
    entry.categoryHash = _categoryHashes[i];
    entry.level = _levels[i];
    entriesCrc = crc32Update(entriesCrc,
                             reinterpret_cast<const uint8_t*>(&entry),
                             sizeof(entry));
  }

  tutor_qidx::Header header{};
  memcpy(header.magic, tutor_qidx::MAGIC, sizeof(header.magic));
  header.version = tutor_qidx::VERSION;
  header.headerSize = sizeof(tutor_qidx::Header);
  header.entrySize = sizeof(tutor_qidx::Entry);
  header.flags = tutor_qidx::FLAGS;
  header.sourceSize = sourceSize;
  header.sourceMtime = sourceMtime;
  memcpy(header.sourceSampleCrc32, sourceSampleCrc32,
         sizeof(header.sourceSampleCrc32));
  header.recordCount = (uint32_t)_offsets.size();
  header.entriesCrc32 = entriesCrc ^ 0xffffffffUL;

  const String tempPath = path + ".tmp";
  const String backupPath = path + ".bak";
  if (_fs->exists(tempPath)) _fs->remove(tempPath);
  File output = _fs->open(tempPath, FILE_WRITE);
  if (!output || !writeExact(output, &header, sizeof(header))) {
    if (output) output.close();
    return false;
  }
  bool writeOk = true;
  for (size_t i = 0; i < _offsets.size(); ++i) {
    tutor_qidx::Entry entry{};
    entry.offset = _offsets[i];
    entry.idHash = _idHashes[i];
    entry.categoryHash = _categoryHashes[i];
    entry.level = _levels[i];
    if (!writeExact(output, &entry, sizeof(entry))) {
      writeOk = false;
      break;
    }
  }
  output.flush();
  output.close();
  if (!writeOk) return false;

  String verifyReason;
  if (!loadQidx(tempPath, sourceSize, sourceMtime, sourceSampleCrc32,
                &verifyReason)) {
    _fs->remove(tempPath);
    return false;
  }

  if (_fs->exists(backupPath)) _fs->remove(backupPath);
  const bool hadExisting = _fs->exists(path);
  if (hadExisting && !_fs->rename(path, backupPath)) return false;
  if (!_fs->rename(tempPath, path)) {
    if (hadExisting && _fs->exists(backupPath)) _fs->rename(backupPath, path);
    return false;
  }
  if (_fs->exists(backupPath)) _fs->remove(backupPath);
  Serial.printf("[DB] %s: qidx rebuilt %s\n", _dataPath.c_str(), path.c_str());
  return true;
}

bool QuestionDB::readLineAt(size_t index, String& line) {
  if (index >= _offsets.size()) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!ensureOpen()) return false;
    if (_file.seek(_offsets[index])) {
      line = _file.readStringUntil('\n');
      if (line.length() > 2) return true;
    }
    _file.close();  // Drop a possibly stale handle and retry once.
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
  return q.id.length() > 0;
}

bool QuestionDB::readAt(size_t index, Question& out) {
  String line;
  return readLineAt(index, line) && parseLine(line, out);
}

bool QuestionDB::metadataAt(size_t index, uint8_t& level,
                            uint32_t& categoryHash) const {
  if (index >= _levels.size() || index >= _categoryHashes.size()) return false;
  level = _levels[index];
  categoryHash = _categoryHashes[index];
  return level != 0;
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
  const uint32_t avoidHash = avoidId.length() ? hashText(avoidId) : 0;
  for (int lv = clampLevel(level); lv >= 1; --lv) {
    size_t index = 0;
    if (pickRandomAtLevel((uint8_t)lv, avoidHash, index) && readAt(index, out)) return true;
  }
  // Last resort: any readable record, starting from a random position.
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
  const uint32_t h = hashText(id);
  for (size_t i = 0; i < _idHashes.size(); ++i) {
    if (_idHashes[i] != h) continue;
    Question q;
    if (readAt(i, q) && q.id == id) { out = q; return true; }
  }
  return false;
}
