# AgentBridge Tailscale Funnel TLS 지원

## 목적

스마트폰 핫스팟에 연결된 CoreS3가 Tailnet 전용 `100.x` 주소 대신 Tailscale Funnel의 공개 HTTPS 주소를 통해 OpenClaw PC Bridge에 안전하게 연결할 수 있도록 한다.

## 변경 범위

- `agentBridge.tls` 설정을 추가하고 생략 시 기존 LAN HTTP 동작을 유지한다.
- TLS 사용 시 `WiFiClientSecure`와 Let’s Encrypt ISRG Root X1/X2를 사용해 인증서 체인과 호스트명을 검증한다.
- 기존 12 KB `agent_bridge` worker에서 HTTPS 요청을 수행하고 완료 시 스택 high-water mark를 기록한다.
- 로그에는 검색어, 인증 키, 응답 본문을 남기지 않는다.
- README, 설정 템플릿, 펌웨어 설계 문서에 LAN HTTP와 Funnel HTTPS 구성을 함께 기록한다.

## 호환성 및 안전 조건

- `tls: false` 또는 필드 생략 시 `http://host:8765/v1/agent` 형식을 유지한다.
- `tls: true`일 때만 `https://host:port/v1/agent`를 사용한다.
- `setInsecure()`는 사용하지 않는다.
- TLS 실패 시 Brave Search로 폴백하지 않고 기존 deferred 결과 경로로 안전한 오류를 전달한다.
- 현재 비동기 검색 변경과 저장소 밖 PC Bridge 계약은 변경하지 않는다.

## 검증

- `m5stack-cores3-realtime-camera`, `m5stack-cores3`를 각각 빌드한다.
- Realtime 카메라 빌드를 USB VID/PID `303A:1001` CoreS3에 플래시한다.
- 실제 Funnel 주소가 준비되면 HTTPS 성공, 인증 오류, DNS 실패, 연결 거부, timeout을 시리얼 로그와 후속 음성으로 검증한다.
