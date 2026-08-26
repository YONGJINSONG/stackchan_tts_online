#include "llm_router.h"
#include "face_mapping.h"
#include "face_recognition_engine.h"
#include "usage_timer.h"
#include <esp_camera.h>
#include <vector>

// ── 다른 모듈에서 이미 구현되어 있다고 가정하는 함수들 ───────────────
extern camera_fb_t* captureImage();                          // 카메라 캡처
extern void releaseImage(camera_fb_t* fb);                   // 프레임 버퍼 반환
extern void speakTTS(const String& text);                    // TTS 출력
extern void savePhotoToSD(camera_fb_t* fb, const String& path);
extern void sendPhotoToPrinter(const String& path);           // BLE ESC/POS 프린터
extern void sendPhotoToDisplay(const String& path);           // e-paper 디스플레이 보드
extern void playStudyProgram(const String& name);             // SD카드 공부 프로그램
extern void playMusicFile(const String& name);                // SD카드 음악 재생
extern String callBraveSearch(const String& query);           // Brave Search API (safesearch=strict)
extern String callGPT4oMiniVision(camera_fb_t* fb);           // GPT-4o mini Vision 사물 인식
// ────────────────────────────────────────────────────────────

static FaceMapping faceMap;
static bool faceMapLoaded = false;

static void ensureFaceMapLoaded() {
  if (!faceMapLoaded) {
    faceMap.load();
    faceMapLoaded = true;
  }
}

// ① 사진 찍기 / 인식
String handleTakePhoto(const String& output) {
  camera_fb_t* fb = captureImage();
  if (!fb) return "카메라 촬영에 실패했어요.";

  String path = "/photos/" + String(millis()) + ".jpg";
  String result;

  if (output == "save") {
    savePhotoToSD(fb, path);
    result = "사진을 저장했어요.";

  } else if (output == "print") {
    savePhotoToSD(fb, path);
    sendPhotoToPrinter(path);
    result = "사진을 프린터로 보냈어요.";

  } else if (output == "display") {
    savePhotoToSD(fb, path);
    sendPhotoToDisplay(path);
    result = "사진을 디스플레이로 보냈어요.";

  } else if (output == "recognize_face") {
    ensureFaceMapLoaded();
    FaceMatchResult m = faceEngine.recognize(fb);
    if (m.found) {
      String name = faceMap.nameForId(m.id);
      result = name.length() ? (name + "이(가) 보여요!") : "등록된 사람이지만 이름을 찾지 못했어요.";
    } else {
      result = "모르는 사람이에요. 등록해드릴까요?";
    }

  } else if (output == "recognize_object") {
    result = callGPT4oMiniVision(fb);  // 예: "책상 위에 곰인형과 동화책이 보여요."

  } else {
    result = "지원하지 않는 사진 처리 방식이에요.";
  }

  releaseImage(fb);
  return result;
}

// ② 얼굴 등록 (가족 구성원, 3장: 정면/좌/우)
String handleEnrollFace(const String& name) {
  if (name.length() == 0) {
    return "누구를 등록할까요? 이름을 알려주세요.";
  }

  ensureFaceMapLoaded();

  static const char* prompts[3] = {
    "카메라를 정면으로 봐주세요",
    "고개를 살짝 왼쪽으로 돌려주세요",
    "이번엔 오른쪽으로 살짝 돌려주세요"
  };

  int startId = faceMap.nextId();
  std::vector<int> newIds;

  for (int i = 0; i < 3; i++) {
    speakTTS(prompts[i]);
    delay(1200);  // 사용자가 자세를 잡을 시간

    bool captured = false;
    for (int attempt = 0; attempt < 3 && !captured; attempt++) {
      camera_fb_t* fb = captureImage();
      if (fb && faceEngine.detectFace(fb)) {
        int newId = startId + i;
        if (faceEngine.enrollWithId(fb, newId)) {
          newIds.push_back(newId);
          captured = true;
        }
        releaseImage(fb);
      } else {
        if (fb) releaseImage(fb);
        speakTTS("얼굴이 잘 안 보여요, 다시 봐주세요.");
        delay(800);
      }
    }

    if (!captured) {
      return "등록에 실패했어요. 다시 시도해주세요.";
    }
  }

  faceMap.setIds(name, newIds);
  faceMap.save();
  return name + " 등록을 완료했어요!";
}

// ③ 얼굴 삭제
String handleDeleteFace(const String& name) {
  ensureFaceMapLoaded();

  std::vector<int> ids = faceMap.remove(name);
  if (ids.empty()) {
    return name + "는 등록되어 있지 않아요.";
  }

  for (int id : ids) {
    faceEngine.deleteId(id);
  }
  faceMap.save();
  return name + " 얼굴 정보를 삭제했어요.";
}

// ④ SD카드 공부 프로그램 / 음악 재생
String handlePlaySdContent(const String& contentType, const String& name) {
  if (contentType == "study_program") {
    pauseUsageTimer();  // 공부 프로그램은 30분 사용 타이머 제외 대상
    playStudyProgram(name);
    return "공부 프로그램을 시작할게요.";

  } else if (contentType == "music") {
    playMusicFile(name);
    return "음악을 재생할게요.";
  }

  return "재생할 수 없는 콘텐츠예요.";
}

// ⑤ 인터넷 검색
String handleWebSearch(const String& query) {
  if (query.length() == 0) {
    return "무엇을 검색할지 말해주세요.";
  }
  // callBraveSearch 내부에서 safesearch=strict 파라미터를 항상 포함해야 함
  String snippet = callBraveSearch(query);
  return snippet.length() ? snippet : "검색 결과를 찾지 못했어요.";
}

// ⑥ 사용 시간 제한 타이머
String handleStartTimer(int durationMinutes) {
  int minutes = (durationMinutes > 0) ? durationMinutes : 30;
  armUsageTimer(minutes);
  return String(minutes) + "분 타이머를 시작했어요.";
}
