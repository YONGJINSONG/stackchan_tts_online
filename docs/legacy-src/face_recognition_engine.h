#pragma once
#include <Arduino.h>
#include <esp_camera.h>

// ESP-DL(HumanFaceDetect + HumanFaceFeat + FaceRecognizer)을 감싸는 래퍼.
// 실제 클래스/메서드명은 사용하는 esp-dl 버전의 예제(human_face_recognition)를 참고해 맞춰야 함.
// 여기서는 앱 레벨에서 필요한 인터페이스만 정의.

struct FaceMatchResult {
  bool found;
  int id;
  float similarity;
};

class FaceRecognitionEngine {
public:
  bool begin();  // 모델 로드, DB 초기화(SD카드 fatfs_sdcard 모드)

  // 카메라 프레임에서 얼굴 1개 검출. 없으면 false.
  bool detectFace(camera_fb_t* fb);

  // 검출된 얼굴 영역에서 feature 추출 후 지정한 id로 DB에 등록
  bool enrollWithId(camera_fb_t* fb, int id);

  // 검출된 얼굴 영역에서 feature 추출 후 DB와 매칭
  FaceMatchResult recognize(camera_fb_t* fb);

  // DB에서 특정 id의 feature 삭제
  bool deleteId(int id);
};

extern FaceRecognitionEngine faceEngine;  // 전역 인스턴스 (main에서 begin() 호출)
