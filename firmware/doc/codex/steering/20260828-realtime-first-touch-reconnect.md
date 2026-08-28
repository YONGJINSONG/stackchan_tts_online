# Realtime 첫 터치 재연결 대기

## 목적

부팅 후 OpenAI Realtime WebSocket이 일시적으로 끊긴 상태에서 사용자가 화면을
터치하면 녹음만 시작되고 오디오가 서버로 전달되지 않아 첫 발화에 반응하지 않는
문제를 해결한다.

## 확인된 원인

- 로그에서 첫 터치 직전 또는 녹음 시작 직후 `[WSc] Disconnected!`가 발생한다.
- 현재 `startRealtimeRecord()`는 WebSocket 및 Realtime 세션 준비 여부를 확인하지
  않고 녹음을 시작한다.
- 끊긴 동안 생성된 `input_audio_buffer.append`는 서버에 도달하지 않으며, 재연결은
  사용자가 두 번째 터치로 녹음을 멈춘 뒤에야 완료된다.

## 대상 범위와 주 변경 파일

- `src/llm/RealtimeLLMBase.h`
- `src/llm/RealtimeLLMBase.cpp`
- `src/llm/ChatGPT/RealtimeChatGPT.cpp`
- `src/llm/Gemini/GeminiLive.cpp` (공통 기반 상태와의 호환이 필요한 부분만)
- `src/mod/AiStackChan/RealtimeAiMod.cpp`

## 구현 방침

1. TCP 연결 여부와 별도로 Realtime 세션 준비 상태를 공통 기반 클래스에서 관리한다.
2. 세션이 준비되지 않았을 때의 터치는 즉시 녹음하지 않고 `청취 대기`로 저장하며
   화면에는 `연결 중...`을 표시한다.
3. ChatGPT의 `session.updated` 또는 Gemini의 `setupComplete`를 받으면 준비 상태로
   전환하고, 대기 중인 청취 요청이 있으면 그때 녹음을 자동 시작한다.
4. 연결 해제 시 실제 녹음을 즉시 중단한다. 사용자가 청취 중이었다면 요청 상태는
   유지하여 재연결 후 자동 복구한다.
5. 연결 대기 중 다시 터치하면 대기 요청을 취소할 수 있도록 토글 판정을 실제 녹음
   상태가 아닌 `녹음 또는 대기 요청` 상태로 통일한다.
6. WebSocket 송신은 기존과 같이 WebSocket task 한 곳에서만 수행하고, 오디오 mutex
   및 Speaker/Mic 전환 순서는 변경하지 않는다.
7. 연결·해제 로그에 Wi-Fi 상태/RSSI, 내부 힙 여유량과 최대 연속 블록을 추가하여
   `start_ssl_client: -1`이 무선 단절인지 TLS용 연속 메모리 부족인지 구분한다.

## 설계상 주의점

- `WStype_CONNECTED` 직후에는 `session.update` 처리 전이므로 아직 녹음을 시작하지 않는다.
- 자동 재청취, 30초 무음 종료, 공부 모드 전환, 강제 취침 해제의 기존 동작을 유지한다.
- 사용자 작업 중인 이미지와 SD 복사본 파일은 건드리지 않는다.
- API 키와 Wi-Fi 비밀번호는 소스나 문서에 기록하지 않는다.

## 확인 방법

1. `m5stack-cores3-realtime` PlatformIO 빌드를 수행한다.
2. 정적 확인으로 ChatGPT/Gemini의 연결·해제·세션 준비 이벤트가 모두 공통 상태를
   갱신하는지 점검한다.
3. 실기에서 부팅 후 WebSocket이 끊긴 타이밍에 한 번만 터치했을 때 `연결 중...` 뒤
   자동으로 `듣는 중...`으로 바뀌고 첫 발화에 응답하는지 확인한다.
4. 연결 대기 중 두 번째 터치로 취소되는지, 대화 응답 후 자동 청취와 30초 종료가
   유지되는지 확인한다.
5. 녹음 직후 강제로 연결이 끊기는 경우 녹음/오디오 송신이 멈추고, 첫 TLS 재시도가
   실패하더라도 다음 재시도에서 세션이 복구되어 자동 청취하는지 확인한다.

## 되돌리기

위 파일들의 세션 준비/청취 대기 상태 변경만 제거하면 기존 즉시 녹음 방식으로
복귀할 수 있다.
