#include "face_mapping.h"
#include <SD.h>
#include <ArduinoJson.h>  // ArduinoJson v7 이상 기준

bool FaceMapping::load(const char* path) {
  path_ = path;
  table_.clear();

  if (!SD.exists(path)) {
    Serial.println("[FaceMapping] 매핑 파일 없음, 빈 테이블로 시작");
    return true;  // 최초 실행 시 정상 상황
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println("[FaceMapping] 파일 열기 실패");
    return false;
  }

  JsonDocument doc;  // 12개 feature 기준 소량 데이터라 크기 제한 불필요
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[FaceMapping] JSON 파싱 실패: %s\n", err.c_str());
    return false;
  }

  for (JsonPair kv : doc.as<JsonObject>()) {
    std::vector<int> ids;
    for (JsonVariant v : kv.value().as<JsonArray>()) {
      ids.push_back(v.as<int>());
    }
    table_[String(kv.key().c_str())] = ids;
  }

  Serial.printf("[FaceMapping] %d명 로드 완료\n", table_.size());
  return true;
}

bool FaceMapping::save(const char* path) {
  const char* target = path ? path : path_.c_str();

  JsonDocument doc;
  for (auto const& entry : table_) {
    JsonArray arr = doc[entry.first].to<JsonArray>();
    for (int id : entry.second) arr.add(id);
  }

  File f = SD.open(target, FILE_WRITE);
  if (!f) {
    Serial.println("[FaceMapping] 저장용 파일 열기 실패");
    return false;
  }

  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

void FaceMapping::setIds(const String& name, const std::vector<int>& ids) {
  table_[name] = ids;
}

std::vector<int> FaceMapping::remove(const String& name) {
  std::vector<int> removed;
  auto it = table_.find(name);
  if (it != table_.end()) {
    removed = it->second;
    table_.erase(it);
  }
  return removed;
}

String FaceMapping::nameForId(int id) const {
  for (auto const& entry : table_) {
    for (int candidate : entry.second) {
      if (candidate == id) return entry.first;
    }
  }
  return "";
}

std::vector<int> FaceMapping::idsForName(const String& name) const {
  auto it = table_.find(name);
  return (it != table_.end()) ? it->second : std::vector<int>{};
}

int FaceMapping::nextId() const {
  int maxId = 0;
  for (auto const& entry : table_) {
    for (int id : entry.second) maxId = max(maxId, id);
  }
  return maxId + 1;
}

bool FaceMapping::exists(const String& name) const {
  return table_.find(name) != table_.end();
}
