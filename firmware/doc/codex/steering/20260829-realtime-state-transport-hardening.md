# CoreS3 Realtime 상태·전송 안정화

## 목적

- 세션 준비 전 터치와 연결 단절 뒤의 듣기 의도를 안전하게 보존한다.
- 송신 실패나 `session.updated` 누락이 영구 `연결 중...` 상태로 남지 않게 한다.
- OpenAI Realtime 입력을 실제 24 kHz PCM으로 맞추되 Gemini의 16 kHz 경로는 유지한다.

## 대상 범위

- `src/llm/RealtimeLLMBase.*`: 잠금된 듣기 상태, 60초 대기 만료, 제공자별 캡처 설정
- `src/llm/ChatGPT/RealtimeChatGPT.*`: 송신 결과 확인, WS 태스크 reconnect 큐, 세션 준비 watchdog
- `src/mod/AiStackChan/RealtimeAiMod.cpp`: 대기 중 두 번째 터치 취소 및 상태 표시

## 구현 방침

1. 듣기 요청·세션 준비·실제 녹음 상태를 하나의 critical section 아래에서 전이하고 일관된 snapshot으로 조회한다.
2. 사용자 취소와 모드 종료는 듣기 의도를 지우고, 전송 단절은 실제 녹음만 멈춘 채 의도를 보존한다.
3. 대기 중 두 번째 터치는 취소하며, 준비되지 않은 요청은 60초 후 자동 만료한다.
4. 모든 reconnect 실행은 WebSocket 태스크의 tick에서만 수행한다. `session.update`/audio append 송신 실패와 연결 후 15초 동안 `session.updated`가 없는 경우 reconnect를 예약한다.
5. Realtime 공통 버퍼는 최대 3,000 samples를 수용하고, ChatGPT는 24 kHz/3,000 samples, Gemini는 16 kHz/2,000 samples를 사용한다.
6. 모델 `gpt-realtime`과 현재 `server_vad` 설정은 변경하지 않는다.

## 설계상 주의점

- `WebSocketsClient`는 단일 태스크에서만 접근한다.
- 녹음 상태를 변경하는 critical section 안에서 Serial, 화면 갱신, I2S 호출 같은 블로킹 작업을 수행하지 않는다.
- SFX, 응답 재생, 모드 전환의 기존 Mic/Speaker mutex 순서를 보존한다.
- 사용자 소유 미추적 백업과 음성 변경에 무관한 PNG는 수정하지 않는다.

## 확인 방법

- `m5stack-cores3-realtime-camera`, `m5stack-cores3-realtime`, `m5stack-core2-realtime` 빌드
- COM3 업로드가 수동 reset 없이 `Writing at...`에 진입하는지 확인
- 준비 전 1회 터치, 첫 음성 왕복, 녹음 중 Wi-Fi 5초 단절, 추가 터치 없는 자동 재개, 두 번째 음성 왕복을 하나의 시리얼 로그에서 확인
- 로그에서 capture/wire rate, samples/bytes, 송신 성공 여부, reconnect 사유를 확인

## 2026-08-29 구현·자동 검증 결과

- 순수 `RealtimeListenState`와 호스트 테스트를 추가해 준비 전 요청, 취소, 단절 후 의도 보존, ready 후 1회 재개, 발화 중 녹음 금지, 60초 만료, 타이머 wrap, 모드 종료 취소를 검증했다.
- `m5stack-cores3-realtime-camera`: 성공, RAM 28.7%, Flash 54.1%.
- `m5stack-cores3-realtime`: 성공, RAM 24.8%, Flash 52.9%.
- `m5stack-core2-realtime`: 성공, RAM 1.8%, Flash 53.5%.
- COM3 자동 업로드는 수동 버튼 없이 `Writing at...`, 전 구간 hash 검증, `SUCCESS`까지 통과했다.
- 실제 발화, 발화 중 AP 5초 단절, 무터치 자동 재개, 두 번째 응답 및 얼굴 자산 육안 검사는 물리 조작이 필요하므로 별도 실기 결과로 판정한다.
- OpenAI 24 kHz 직접 캡처의 2회 안정성 판정 전이므로 리샘플러 대체 경로는 아직 활성화하지 않았다.
