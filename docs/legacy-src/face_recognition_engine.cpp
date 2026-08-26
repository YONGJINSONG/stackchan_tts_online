#include "face_recognition_engine.h"

// 실제 esp-dl 헤더는 사용 버전에 맞춰 include (예시 경로, 버전별로 다를 수 있음)
// #include "human_face_detect.hpp"
// #include "human_face_recognition.hpp"

FaceRecognitionEngine faceEngine;

// NOTE: 아래 구현은 esp-dl human_face_recognition 예제(app_main.cpp)의 흐름을 앱 레벨
// 클래스로 감싼 형태다. 실제 프로젝트에서는 espressif/esp-who의
// examples/human_face_recognition 소스를 참고해 dl::detect::HumanFaceDetect,
// dl::recognition::FaceRecognizer(또는 사용 버전의 대응 클래스)로 교체해야 한다.

namespace {
  // static dl::detect::HumanFaceDetect detector;
  // static dl::recognition::FaceRecognizer recognizer(DB_FATFS_SDCARD);
  bool modelReady = false;
}

bool FaceRecognitionEngine::begin() {
  // detector.init();
  // recognizer.init("/sdcard/face_db");  // DB_FATFS_SDCARD 모드 초기화
  modelReady = true;
  Serial.println("[FaceEngine] 모델/DB 초기화 완료");
  return modelReady;
}

bool FaceRecognitionEngine::detectFace(camera_fb_t* fb) {
  if (!modelReady || !fb) return false;
  // auto results = detector.run(fb);
  // return !results.empty();
  return true;  // TODO: 실제 검출 결과로 교체
}

bool FaceRecognitionEngine::enrollWithId(camera_fb_t* fb, int id) {
  if (!modelReady || !fb) return false;
  // auto faces = detector.run(fb);
  // if (faces.empty()) return false;
  // auto feature = recognizer.extractFeature(fb, faces[0]);
  // return recognizer.enroll(id, feature);
  Serial.printf("[FaceEngine] id=%d feature 등록 (stub)\n", id);
  return true;  // TODO: 실제 등록 결과로 교체
}

FaceMatchResult FaceRecognitionEngine::recognize(camera_fb_t* fb) {
  FaceMatchResult result{false, -1, 0.0f};
  if (!modelReady || !fb) return result;

  // auto faces = detector.run(fb);
  // if (faces.empty()) return result;
  // auto feature = recognizer.extractFeature(fb, faces[0]);
  // auto match = recognizer.recognize(feature);  // {id, similarity} 유사 구조
  // result.found = match.similarity >= FACE_SIM_THRESHOLD;
  // result.id = match.id;
  // result.similarity = match.similarity;

  // TODO: 위 실제 호출로 교체. 아래는 임시 스텁 값.
  result.found = false;
  return result;
}

bool FaceRecognitionEngine::deleteId(int id) {
  if (!modelReady) return false;
  // return recognizer.deleteFeature(id);
  Serial.printf("[FaceEngine] id=%d feature 삭제 (stub)\n", id);
  return true;
}
