# Realtime SSL OOM + CoreS3 invalid servo pins

## 증상 (시리얼)
- `This pin can not be a servo: 33/32` — SD yaml이 아직 Core2 핀
- `SSL - Memory allocation failed` + WS Disconnected — data_refresh HTTPS가 Realtime TLS와 내부 RAM 경쟁

## 수정
1. `FunctionCall.cpp` data_refresh: WS 연결 중에는 HTTPS 보류, fetch 사이 delay
2. `Robot.cpp`: CoreS3에서 핀 32/33이면 DEFAULT(1/2)로 교정
