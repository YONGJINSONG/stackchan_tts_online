#pragma once
#include <Arduino.h>

// GPT-4o mini에 사용자 발화(STT 결과 텍스트)를 보내고,
// Function Calling 결과에 따라 각 기능 핸들러로 라우팅한 뒤
// 최종 자연어 응답 텍스트를 반환한다 (이후 TTS로 전달).
//
// 대화 기록(messages)은 세션 동안 누적해서 매 호출에 함께 전송해야
// 문맥(예: "이름이 뭐예요?" -> "아빠")이 유지된다.

String routeUserUtterance(const String& userText);

// 각 기능 핸들러 (실제 하드웨어 제어 로직은 각 모듈에서 구현, 여기선 시그니처만 정의)
String handleTakePhoto(const String& output);          // save/print/display/recognize_face/recognize_object
String handleEnrollFace(const String& name);
String handleDeleteFace(const String& name);
String handlePlaySdContent(const String& contentType, const String& name);
String handleWebSearch(const String& query);
String handleStartTimer(int durationMinutes);
