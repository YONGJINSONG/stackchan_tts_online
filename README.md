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

Google Cloud TTS는 [Text-to-Speech API](https://cloud.google.com/text-to-speech)를 켠 뒤 API 키를 `apikey.tts`에 넣는다.

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
| 인터넷 검색 (Brave, safesearch strict) | Function Calling `web_search` |
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
