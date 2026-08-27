# 연속 대화 복원 및 「공부하자」freeze 수정

## 목적

최초 터치 후 응답마다 자동으로 청취를 재개하는 기존 Realtime 대화 흐름을 복원한다. `study_program` function_call은 후속 응답을 만들지 않고 WebSocket task에서 오디오를 정상 정리한 뒤 KidsTutor로 전환해 `speaking`/audio mutex 교착을 막는다.

## 변경

1. `FunctionCall` — `play_sd_content(study_program)`이 내부 모드 전환 요청임을 Realtime 처리부에 전달한다.
2. `RealtimeChatGPT.cpp` — 일반 응답 종료 시 mic를 자동 재개한다. 공부 모드 요청은 function output만 기록하고 추가 `response.create` 없이, WebSocket task에서 Speaker/Mic/mutex/speaking을 정리한다.
3. `KidsTutorMod.cpp` — 메인 루프는 Realtime `speaking`이 정상 종료될 때까지만 기다린다. `Speaker.isPlaying()` 기반 무한 대기와 타 task에서의 강제 오디오 상태 해제를 제거한다.
4. 공부 모드 전환 시 Realtime은 Speaker를 종료하되 Mic를 재시작하지 않는다. KidsTutor도 세션 시작 직전에 Mic를 켜지 않으며, 버튼 입력 모드의 로컬 WAV 재생 후에는 Mic를 꺼진 상태로 유지해 `mic_task`/I2S 경합을 막는다.
5. KidsTutor init/SD 오류 처리와 기존 로그는 유지한다.

## 확인

- 최초 터치 후 일반 응답이 끝나면 `Start realtime recording`으로 자동 복귀하고, 30초 무음 시 종료한다.
- `[kids] voice start queued` 후 `study mode switch: audio released` → `opening queued` → `transition` 순서로 진행한다.
- 첫 `[LOCAL WAV]` 재생에서 `Mic_Class::mic_task`/`i2s_stop` panic 없이 다음 문제 화면으로 진행한다.
- 날씨/시간 등 다른 function_call은 후속 음성 응답과 자동 재청취를 유지한다.
- `m5stack-cores3-realtime`, `m5stack-core2-realtime` 빌드를 확인한다.
