# 「공부하자」freeze 잔여 수정

## 목적

Realtime function_call 후 `speaking`이 풀리지 않아 `kids_tutor_process_pending`이 영원히 대기하는 soft-hang를 막고, KidsTutor init/SD 경로 로그·오디오 정리를 보강한다.

## 변경

1. `KidsTutorMod.cpp` — pending 시각 기록 + speaking 대기(스피커 재생 중 대기 / 무음 2.5s 후 강제 / 절대 8s 타임아웃)
2. `RealtimeChatGPT.cpp` — `response.output_audio.delta`에서 speaking 미설정 시 mic→speaker 핸드오프; function_call `response.done`에서 무음이면 speaking 정리 가능하도록 주석/정리
3. KidsTutor `ensureReady`/`init` 로그 세분화 + session 시작 전 Speaker/Mic 정리

## 확인

- `[kids] voice start queued` 후 수 초 내 `opening queued` / `transition`
- speaking stuck 시 `[kids] no follow-up audio` 또는 `speaking wait timed out` 로그
