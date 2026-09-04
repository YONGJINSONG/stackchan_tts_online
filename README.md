# StackChan Multifunction Assistant

M5Stack CoreS3 (ESP32-S3) 기반 아동용 멀티기능 스택챤 어시스턴트.
캐스케이드 음성 파이프라인(Whisper → GPT-4o mini → Google Cloud TTS)과 Function Calling 의도 라우팅 위에
챗봇 대화, 사진/사물 인식, SD카드 콘텐츠 재생, 인터넷 검색, 사용 시간 타이머를 올린다.

[stack-chan-ko](https://github.com/lovida8254/stack-chan-ko)(한국판 스택짱)를 포크해 베이스로 삼았다.
RoboEyes 표정·서보 제스처·한국어 웹 설정 페이지·한국 생활 데이터 연동·웨이크워드는
베이스에서 가져왔고, 음성 경로는 비용 때문에 OpenAI Realtime 대신 캐스케이드를 기본으로 둔다.

## 벤더 클라우드를 두지 않는다

기기가 OpenAI·Google API를 **직접** 호출한다. 중간에 벤더 백엔드(xiaozhi.me 같은)를 두지 않는다.
[warble](https://github.com/rebelthor/warble)이 제기한 문제 — 로봇이 특정 회사 서버에 묶이고
계정이 살아 있어야만 동작하는 구조 — 를 애초에 만들지 않는다는 뜻이다.
warble의 PC 로컬 백엔드(whisper.cpp + Ollama + Piper)는 **넣지 않는다**.
API 키는 SD카드의 `Copy-to-SD/yaml/SC_SecConfig.yaml`에 본인이 넣고 본인이 소유하며,
저장소에는 `********` 템플릿만 들어간다.

대신 클라우드 API 사용료는 발생한다. 캐스케이드 기준 추산은
`docs/stackchan_voice_pipeline_design.md`에 정리되어 있다.

저지연이 필요하면 `m5stack-cores3-realtime`으로 Realtime API를 쓸 수 있다. 기본 빌드는 아니다.

---

## 저장소 구조

```
stackchan_project/
├── firmware/            펌웨어 (PlatformIO, Arduino framework) — stack-chan-ko 베이스
│   ├── src/             본체 소스 (llm/ stt/ tts/ kids_tutor/ face/ driver/ mod/ ...)
│   ├── lib/             RoboEyes, m5stack-avatar 등 벤더링된 라이브러리
│   ├── incbin/          펌웨어에 구워지는 한국어 웹 설정 페이지
│   └── platformio.ini   보드별 빌드 환경
├── Copy-to-SD/          SD카드 루트에 복사할 설정 파일 (WiFi/API키/하드웨어/기능 옵션)
├── doc/                 베이스 프로젝트 문서 (기능별 사용법, upstream README)
├── docs/                이 프로젝트의 설계 문서
│   ├── stackchan_voice_pipeline_design.md   음성 파이프라인 아키텍처·비용 산정
│   ├── stackchan_multifunction_design.md    6개 기능 전체 설계, 얼굴 등록 흐름
│   ├── stackchan_function_schemas.json      Function Calling tool 스키마
│   └── legacy-src/      포크 이전에 작성한 프로토타입 (이식 대상, 빌드되지 않음)
├── images/              데모 이미지
├── LICENSE              GPL-3.0-or-later
├── NOTICE.md            서드파티 저작권 고지
└── UPSTREAM.md          업스트림 관계와 동기화 방법
```

---

## 빌드

PlatformIO Core가 필요하다 (`pip install -U platformio`).
툴체인·패키지를 C:가 아닌 곳에 두려면 `PLATFORMIO_CORE_DIR`을 지정한다 (예: `F:\.platformio`).

```bash
cd firmware

# 기본: 캐스케이드 (Whisper → GPT-4o mini → Google Cloud TTS)
python -m platformio run -e m5stack-cores3

# 빌드 + 플래시 (포트는 환경에 맞게)
python -m platformio run -e m5stack-cores3 -t upload --upload-port COM3
```

카메라가 필요한 기능(사진 저장·사물 인식)은 `m5stack-cores3-camera` 환경을 쓴다.
CoreS3에서 카메라 SCCB가 근접센서·IMU·전원·터치와 내부 I2C 버스를 공유하며,
**공존이 아직 검증되지 않았다**. 이 환경은 실기 실험용으로 취급한다.

SD카드 설정은 `Copy-to-SD/` 내용을 카드 루트에 복사한 뒤 채운다.

- `yaml/SC_SecConfig.yaml` — WiFi, `apikey.aiservice`(OpenAI), `apikey.stt`(Whisper, 보통 동일 키), `apikey.tts`(Google Cloud TTS), 선택 `apikey.search`(Brave Search)
- Wi-Fi 연결에 실패하면 SoftAP `StackChan-Setup`에 붙어 `http://192.168.4.1`에서 SSID/비밀번호를 설정한다 (저장 후 재부팅).
- `face/` — `base`/`eyes`/`mouth`/`blush`/`fx` 320×240 PNG가 있으면 레이어 얼굴을 쓴다 (없으면 벡터 Face / RoboEyes).
- `yaml/SC_BasicConfig.yaml` — 서보·핀
- `app/AiStackChanEx/SC_ExConfig.yaml` — LLM/TTS/STT 선택 (기본: ChatGPT + Whisper + Google TTS)
- 음악 mp3는 `app/AiStackChanEx/music/` 에 둔다
- **공부 프로그램**: 이웃 프로젝트 `stackchan_kids_tutor_jiwoo_lite_v4_5_1/sdcard/kids_tutor/` 전체를 SD **루트**에 `/kids_tutor/` 로 복사한다 (문제 DB·`audio/text` WAV·config·progress). 저장소에는 넣지 않는다.
- 문제 DB를 추가하거나 수정한 뒤 `python firmware/scripts/gen_tutor_index.py --db-dir <SD드라이브>:\kids_tutor\db`를 실행하면 첫 시작부터 빠른 `.qidx` 인덱스를 사용한다. 생략해도 기기가 변경된 DB만 한 번 스캔해 자동 생성한다.

Google Cloud TTS는 [Text-to-Speech API](https://cloud.google.com/text-to-speech)를 켠 뒤 API 키를 `apikey.tts`에 넣는다.

### OpenClaw PC Bridge

PC Bridge는 OpenClaw 자체가 아니라 CoreS3의 요청을 OpenClaw Gateway로 전달하는
별도 프로세스다. 이 저장소에는 CoreS3용 Bridge 클라이언트만 포함되며 PC Bridge
서버는 OpenClaw가 설치된 PC에서 별도로 실행되어 있어야 한다.

ChatGPT Realtime의 `web_search`는 WebSocket task에서 Brave HTTPS를 직접 실행하지
않고 `AgentBridgeClient`의 12 KB worker를 통해 PC Bridge와 OpenClaw에 위임한다.
Cascade의 `web_search`는 기존 Brave Search 경로를 유지한다. PC Bridge가 실패해도
Realtime은 WebSocket task에서 Brave Search로 재시도하지 않는다.

기존 LLM과 로봇의 로컬 Function Calling을 유지하면서 일기, PC 장기 기억, 복합
검색, 상품 비교만 OpenClaw에 위임할 수 있다. 연결 방식은 같은 LAN의 HTTP와
Tailscale Funnel의 공개 HTTPS 중 하나를 선택한다.

같은 LAN에서는 Bridge를 `0.0.0.0:8765`에 바인딩하고 다음처럼 설정한다.

```yaml
agentBridge:
  enabled: true
  host: "192.168.0.20"  # PC의 LAN IPv4
  port: 8765
  tls: false
  profile: "kids"       # kids 또는 adult
  deviceId: "roni"
  key: "********"       # PC Bridge의 X-Stackchan-Key 값
```

- 같은 LAN의 다른 기기에서 `http://<PC-IP>:8765/health`가 열리지 않으면 Bridge의 LAN 바인딩과 PC 방화벽 TCP 8765 인바운드 규칙을 확인한다.
- PC 주소가 바뀌지 않도록 공유기에서 DHCP 주소를 예약하는 것을 권장한다.

CoreS3가 스마트폰 핫스팟 등 Tailnet 밖에 있다면 OpenClaw PC의 Bridge를
`127.0.0.1:8765`에서 실행하고 그 PC에서 Tailscale Funnel reverse proxy를 시작한다.
[Tailscale Funnel](https://tailscale.com/docs/features/tailscale-funnel)은 공개 HTTPS를
제공하며 443, 8443, 10000 포트를 지원한다. MagicDNS, HTTPS 인증서, Funnel 권한이
먼저 활성화되어 있어야 한다.

```powershell
tailscale funnel --bg 8765
tailscale funnel status
```

`status`가 표시한 HTTPS 주소에서 `https://`를 제외한 호스트명을 사용한다.

```yaml
agentBridge:
  enabled: true
  host: "openclaw-pc.example-tailnet.ts.net"
  port: 443
  tls: true
  profile: "kids"
  deviceId: "roni"
  key: "32바이트 이상의 임의 키"
```

- `host`에는 `http://`, `https://`, 경로를 넣지 않는다. `127.0.0.1`과 `localhost`도 사용할 수 없다.
- `tls`를 생략하거나 `false`로 두면 기존 LAN HTTP 동작을 유지한다. Funnel은 `tls: true`, `port: 443`으로 사용한다.
- Funnel HTTPS는 Let’s Encrypt ISRG Root X1/X2로 인증서 체인과 호스트명을 검증하며 `setInsecure()`를 사용하지 않는다.
- Tailscale HTTPS 인증서 동작은 [공식 HTTPS 인증서 문서](https://tailscale.com/docs/how-to/set-up-https-certificates)를 참고한다.
- Tailscale을 끈 휴대폰에서 `https://<Funnel-host>/health`가 열리는지 먼저 확인한다.
- Funnel은 인터넷에 공개되므로 32바이트 이상의 강한 `X-Stackchan-Key`, 요청 크기 제한, rate limit을 Bridge에 적용하고 `/health`에는 비밀정보를 반환하지 않는다.
- 이 key는 OpenClaw token이 아니다. OpenClaw token은 PC에만 보관한다.
- 설정 템플릿은 `Copy-to-SD/yaml/SC_SecConfig.yaml`이며, 실제 key는 저장소에 커밋하지 않는다.
- 연결을 끄려면 `enabled: false`로 바꾸거나 `agentBridge` 블록을 제거한다.

PC에서 실제 요청 계약은 다음처럼 확인할 수 있다. 올바른 키는 HTTP 200과
`{"text":"..."}`를 반환하고 잘못된 키는 401 또는 403이어야 한다.

```powershell
$headers = @{ "X-Stackchan-Key" = "32바이트 이상의 임의 키" }
$body = @{ profile="kids"; device_id="roni"; action="search"; text="테스트" } | ConvertTo-Json
Invoke-RestMethod "https://<Funnel-host>/v1/agent" -Method Post -Headers $headers -ContentType "application/json" -Body $body
```

### 공부 프로그램 쓰는 법

음성으로 「공부하자」「영어 하자」「수학」이라고 하거나, 웹 설정 → **공부 프로그램**에서 버튼을 누르면 KidsTutor 모드로 들어간다.

| 선택 | 내용 |
|---|---|
| 데일리 / 공부 | 적응형 10분 (영어+소마+팩토) |
| 영어 | 영어 자유학습 |
| 수학 | 수학 자유학습 |
| 6세 수학 | math_6yo DB가 있을 때 |

세션 중 버튼: **A** 이전 보기 / **B** 확인 / **C** 다음 보기. 문제는 SD WAV로 읽어 준다. 답은 버튼으로만 고른다 (Whisper 답변은 없음). 공부 중에는 사용 시간 타이머가 일시정지된다.

---

## 기능 로드맵

베이스에서 이미 동작하는 것과 이 프로젝트에서 추가한 것을 구분한다.

| 기능 | 상태 |
|---|---|
| 한국어 음성 대화 (Whisper → GPT-4o mini → Google TTS) | 캐스케이드 기본 |
| RoboEyes 표정 · 서보 제스처 · 웨이크워드 | 베이스 제공, 캐스케이드에서 사용 |
| 한국어 웹 설정 페이지 · 페르소나 · 장기 기억 | 베이스 제공 (기본 페르소나는 아동용) |
| 한국 생활 데이터 (날씨·미세먼지·급식·할일·일정) | 베이스 제공, 우리 지역/학교로 교체 필요 |
| 사용 시간 제한 타이머 (30분, 공부 중 일시정지) | Function Calling `start_timer` |
| SD카드 음악 재생 | Function Calling `play_sd_content` (music) |
| SD카드 공부 프로그램 (kids_tutor) | `play_sd_content(study_program)` + KidsTutor 모드. SD `/kids_tutor/` 필요 |
| 인터넷 검색 | Function Calling `web_search`: ChatGPT Realtime은 PC Bridge/OpenClaw, Cascade는 Brave(safesearch strict) |
| 사진 촬영 → SD 저장 | `m5stack-cores3-camera` + `take_photo(save)` |
| 사물 인식 (Vision) | `m5stack-cores3-camera` + `take_photo(recognize_object)` |
| 사진 → BLE 감열 프린터 | **추가 예정** |
| 사진 → 6색 e-paper 보드 | **추가 예정** |
| 얼굴 등록·인식·삭제 | **추가 예정** — 실행 위치 미결정 (아래 참조) |

### 미결정 사항

**얼굴 인식을 어디서 돌릴 것인가.** 온디바이스 ESP-DL은 Arduino 프레임워크에 얹기 까다롭고
베이스에 vendoring되어 있지 않다. 클라우드 비전으로 보내면 아이 얼굴이 외부로 나간다.
후순위로 미루는 선택지도 있다. 카메라 I2C 공존 검증 결과를 보고 정한다.

나머지 미결정 항목(e-paper 전송 방식과 디더링, BLE 프린터 ESC/POS 명령셋,
카메라 모듈, 얼굴 유사도 임계값)은 `docs/stackchan_multifunction_design.md` 5장에 있다.

---

## 라이선스

베이스가 GPL-3.0-or-later 구성요소(FluxGarage RoboEyes)를 포함하므로 이 저장소도
**GPL-3.0-or-later**로 배포된다. 루트 `LICENSE` 참조.
서드파티 저작권은 `NOTICE.md`, 업스트림 동기화는 `UPSTREAM.md`에 정리되어 있다.
