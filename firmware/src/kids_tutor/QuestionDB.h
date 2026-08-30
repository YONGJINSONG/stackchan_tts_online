#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "Question.h"
#include "share/SpiRamStlAllocator.h"

class QuestionDB {
public:
  bool begin(fs::FS& fs, const char* dataPath, const char* indexPath);
  size_t size() const { return _offsets.size(); }

  // Highest level that actually exists in this database. math.ndjson only goes
  // up to 2, so callers must not assume the compile-time MAX_LEVEL is reachable.
  uint8_t maxAvailableLevel() const { return _maxLevel; }

  bool randomByLevel(uint8_t level, Question& out, const String& avoidId = "");
  bool findById(const String& id, Question& out);
  bool readAt(size_t index, Question& out);
  bool metadataAt(size_t index, uint8_t& level, uint32_t& categoryHash) const;
  static uint32_t hashText(const String& text);

private:
  fs::FS* _fs = nullptr;
  String _dataPath;
  File _file;  // Held open: reopening per record costs milliseconds on SPI SD.

  SpiRamVector<uint32_t> _offsets;
  // Parallel to _offsets so level filtering and id lookup never touch the card.
  SpiRamVector<uint8_t> _levels;
  SpiRamVector<uint32_t> _idHashes;
  SpiRamVector<uint32_t> _categoryHashes;
  uint8_t _maxLevel = 1;

  void clearMetadata();
  bool sourceIdentity(uint32_t& sourceSize, uint64_t& sourceMtime,
                      uint32_t sourceSampleCrc32[4]);
  bool loadQidx(const String& path, uint32_t sourceSize, uint64_t sourceMtime,
                const uint32_t sourceSampleCrc32[4], String* reason = nullptr);
  bool scanMetadata();
  bool writeQidx(const String& path, uint32_t sourceSize, uint64_t sourceMtime,
                 const uint32_t sourceSampleCrc32[4]);
  bool ensureOpen();
  bool readLineAt(size_t index, String& line);
  uint8_t clampLevel(uint8_t level) const;
  bool pickRandomAtLevel(uint8_t level, uint32_t avoidHash, size_t& index) const;
  bool parseLine(const String& line, Question& out);
};
