#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

// SD카드에 저장되는 "이름 <-> ESP-WHO feature ID 목록" 매핑 테이블 관리
// 파일 형식 예: {"아빠":[1,2,3],"엄마":[4,5,6],"첫째":[7,8,9],"둘째":[10,11,12]}

class FaceMapping {
public:
  // SD카드에서 매핑 테이블 로드. 파일이 없으면 빈 테이블로 시작.
  bool load(const char* path = "/face_mapping.json");

  // 현재 메모리 상태를 SD카드에 저장 (전체 덮어쓰기)
  bool save(const char* path = "/face_mapping.json");

  // 특정 이름에 id 목록 추가/갱신 (등록 완료 시 호출)
  void setIds(const String& name, const std::vector<int>& ids);

  // 특정 이름 삭제. 삭제된 id 목록을 반환 (ESP-DL feature DB에서 삭제할 때 사용)
  std::vector<int> remove(const String& name);

  // id로 이름 역조회. 못 찾으면 빈 문자열 반환
  String nameForId(int id) const;

  // 이름으로 id 목록 조회
  std::vector<int> idsForName(const String& name) const;

  // 다음 등록 시 사용할 새 id 시작값 (현재 최대 id + 1)
  int nextId() const;

  bool exists(const String& name) const;

private:
  std::map<String, std::vector<int>> table_;
  String path_;
};
