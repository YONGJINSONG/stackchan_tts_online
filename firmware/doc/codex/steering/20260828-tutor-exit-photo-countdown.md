# KidsTutor 챗봇 복귀·사진 카운트다운·모드 복원

## 목적

- 공부 나가기가 모드 순환이 아니라 Realtime 챗봇으로 돌아가게 한다.
- 나가기는 오른쪽 위 버튼으로 둔다.
- `사진 찍어주세요`는 3-2-1 뒤에 촬영한다.
- 촬영본이 포토프레임에 보이게 `photo`/`photos` 경로를 맞춘다.
- 포모도로와 ESP-NOW Remote를 좌우 스와이프 사이클에 다시 넣는다.

## 대상

- `KidsTutorMod` / `StackchanUI`
- `ModManager` (`change_mod_chatbot`)
- `RealtimeAiMod` / `AiStackChanMod` (`modName`)
- `CameraAction` / `Camera.cpp` / `PhotoFrameMod`
- `main.cpp` `init_mod`

## 주의

- Realtime 녹음 중 스피커 비프는 I2S 경합을 피하고 말풍선 숫자만 쓴다.
- ESP-NOW 모드는 진입 시 Wi-Fi를 끊고, 나갈 때 재연결한다.
- 10문제 완료는 기존처럼 공부 메뉴로 돌아가고, 명시적 나가기만 챗봇 복귀.
