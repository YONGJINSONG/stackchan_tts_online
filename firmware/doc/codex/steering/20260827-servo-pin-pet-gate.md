# CoreS3 서보 핀 + 쓰담(pet) 게이트 수정

## 목적

- CoreS3에서 서보가 안 움직이는 원인을 SD YAML 핀(Core2용 33/32)으로 보고 Port A(1/2)로 맞춤.
- 캐스케이드(`AiStackChanMod`)에서도 쓰담 → Happy/blush/제스처가 동작하도록 `g_inAiMod`를 켬.

## 대상 범위

- `Copy-to-SD/yaml/SC_BasicConfig.yaml` — servo pin X/Y
- `firmware/src/StackchanExConfig.h` — CoreS3 DEFAULT_SERVO_PIN (YAML 없을 때 fallback)
- `firmware/src/mod/AiStackChan/AiStackChanMod.cpp` — init/pause에서 `g_inAiMod`

## 구현 방침

1. YAML / 기본 핀: CoreS3 Port A `x:1`, `y:2` (서보가 Port C면 사용자가 18/17로 되돌리면 됨).
2. `AiStackChanMod::init()` → `g_inAiMod = true`, `pause()` → `false`.
3. `g_inAiMod` 심볼은 기존처럼 `RealtimeAiMod.cpp`에 두고 `extern`으로 참조 (캐스케이드 빌드에서도 해당 TU가 컴파일됨).
4. pet 발화(`speakOnPet`)는 Realtime LLM 경로만 유지. 캐스케이드는 표정·제스처·blush만 기대.

## 확인 방법

- 부팅 시리얼: `Servo pin X: 1` / `Y: 2`
- AI 대화 모드에서 화면을 천천히 드래그 → `[touch] stroke` / `[pet] reaction fired` + shy blush
- 모드를 포토프레임/키즈로 넘기면 쓰담 반응 없음
