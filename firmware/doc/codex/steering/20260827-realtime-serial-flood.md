# Realtime 연결 직후 화면 무반응 — session JSON 시리얼 폭주 + DNS

## 목적

연결 직후 거대한 session.update JSON이 Serial에 통째로 출력되어 USB-CDC/WS 태스크가 막히고, DNS 실패로 재연결 루프가 돌며 화면 터치가 안 되는 것처럼 보이는 문제를 완화한다.

## 변경

- `RealtimeChatGPT.cpp` / `GeminiLive.cpp`: session update JSON full dump 제거 또는 짧은 요약만 로그
- Disconnected/DNS 로그는 유지하되 과도한 pretty-print 금지

## 확인

- 부팅 후 시리얼에 tools 전체 JSON이 반복되지 않음
- `session.updated` 후 「터치해서 시작」/터치 반응
