# FW Design Information
Notes on FW design, etc.

- [Task](#task)
- [ESP-NOW Remote Control Mod](#esp-now-remote-control-mod)
- [CoreS3 Camera and Official Stack-chan Servo](#cores3-camera-and-official-stack-chan-servo)
- [Realtime API Function Calling](#realtime-api-function-calling)
  - [Avatar Expression](#avatar-expression)
- [Web App](#web-app)
  - [Personalize page](#personalize-page)


## Task

| Task name | function | Stack size [bytes] | Priority |
| --- | --- | --- | --- |
| loopTask | Arduino loop task | 8192 | 1 |
| drawLoop | Avatar control | 4 * 1024 | 2 |
| facialLoop | Avatar control | 1024 | 2 |
| lipSync | Lip Sync for avatar| 2048 | 2 |
| servo | Servo control synchronized with the avatar | 2048 | 1 |
| battery_check | Battery level check | 2048 | 1 |
| asyncTtsStreamTask | TTS streaming play | 5 * 1024 | 2 |
| webSocketLoopTask | WebSocket processing for LLM Realtime API | 6 * 1024 | 3 |
| agent_bridge | OpenClaw PC Bridge HTTP/HTTPS request (only while an agent tool is pending) | 12 * 1024 | 1 |

## ESP-NOW Remote Control Mod

初期実装では `src/mod/EspNowRemote` に Receiver 固定の Mod を追加する。

- Wi-Fi channel は `1` 固定。
- ESP-NOW 受信コールバックでは payload を固定バッファへコピーするだけにし、Serial 出力は Mod の `idle()` で行う。
- 標準 firmware の Sender から受けた場合は Arduino `esp_now` の受信 data でアプリ payload が offset `20` から始まるため、offset `20` を優先して標準 8 byte 形式として decode 表示する。8 byte だけ届く場合は offset `0` から decode する。
- ESP-NOW Remote Mod 実行中は `main.cpp` の Avatar gaze 連動 servo task を抑止し、受信した yaw/pitch を `ServoCustom::moveTo()` に渡す。
- yaw `-1280..1280` は servo x `45..-45` 度へ、pitch `0..900` は servo y `0..-30` 度へ変換する。
- payload の speed は `ServoCustom::moveTo()` の移動時間 ms として渡す。
- laser にはまだ反映しない。
- ESP-NOW Mod 実行中は Wi-Fi channel 変更により Web/FTP/Realtime API と干渉する可能性がある。
- ESP-NOW Mod 離脱時は ESP-NOW を停止し、offline mode でなければ Wi-Fi STA の再接続を最大 5 秒待つ。

## CoreS3 Camera and Official Stack-chan Servo

### Pet blush and dizzy reaction

Touch-stroke and normal IMU pet reactions use a three-second `PetReaction`
blush timer. LayeredFace prefers the larger, saturated `blush_pet` overlay and
falls back to `blush_shy` (then its ellipse fallback), so older SD cards remain
usable. The firmware also carries a small recovery copy of `blush_pet.png` and
creates the SD file only when it is missing; an existing customized file is
never overwritten. The 25 Hz IMU poll classifies a deliberate shake by dominant-axis sign
changes instead of raw movement energy. At the default `shakeSensitivity: 6`,
three alternating peaks above about 190 deg/s (two reversals) must occur within
800 ms, and each peak must first fall below 45 percent of threshold to re-arm.
The reaction has a three-second cooldown and displays `eye_puzzled` for 2.5
seconds ahead of blink; if that asset is absent it explicitly falls back to the
surprised eye. Speech, gestures, and camera operation reset/suppress the shake
classifier so speaker vibration and robot motion cannot self-trigger it.

The official CoreS3 Stack-chan profile uses `M5_SCS` servos through the PY32 controller at I2C address `0x6F`. Serial2 uses RX/TX pins 7/6, and servo IDs 1 and 2 control the X/Y axes. CoreS3 GPIO remapping, PWM reattachment, and hard-sweep diagnostics are restricted to the PWM servo type. ESP-NOW movement uses the configured center and limits through `ServoCustom::writeOffset`.

The CoreS3 GC0308 camera uses the board-provided external XCLK (`pin_xclk = -1`) and borrows the already-running `M5.In_I2C` port for SCCB. Camera sessions must not release, reinstall, or restart that I2C port because the same board bus also controls the PY32 servo power path.

Camera capture uses the public `esp_camera_init`, `esp_camera_fb_get`, `esp_camera_fb_return`, and `esp_camera_deinit` lifecycle. Initialization discards two warm-up frames. A missed capture triggers one full driver deinitialization and reinitialization; low-level `cam_take` and `cam_stop`/`cam_start` recovery loops are intentionally avoided.

The camera build keeps Espressif's precompiled `cam_hal` and ESP32-S3 `ll_cam` together. Its stock 32768-byte ceiling produces a 30720-byte internal bounce buffer for QVGA RGB565. Physical tests showed that source-built 7680-byte and 15360-byte LL buffers both produced repeated `EV-EOF-OVF`, while the stock pair was the previously working configuration. EOF queue depth, task affinity, clock selection, stop/start state, and `cam_clk_sel` remain stock. Only the GC0308 sensor unit is overridden. GC0308 register reads can time out after returning the stale sensor-ID byte (`0x9B`), so RGB565, QVGA subsampling, and orientation use checked write-only values (`0x24=0xA6`, page-1 `0x53=0x80`/`0x55=0x01`) plus a software shadow for register `0x14`. Page-0 defaults must not be carried into page 1: setting page-1 `0x55` bit 7 stalls the SCCB bus. The PCLK register stays at the framework default.

GC0308 RGB565 frames are big-endian (`RGB565_BE`) and are retained as high-byte/low-byte pairs in the preview buffer. M5GFX therefore consumes that buffer with `setSwapBytes(false)` (`swap565_t` layout); enabling byte swap corrupts only the LCD preview colors. BMP export explicitly reassembles each big-endian pixel before writing BGR888.

Photo countdown/capture holds the servo at its configured origin and rejects queued expression, gaze, web, and ESP-NOW movement until the action ends. Kids Tutor uses the same motion hold while questions are active. After the completion sound finishes, the hold is released and the normal gesture dance is queued; the dance sequence ends at the origin.

Kids Tutor stores its long-lived database metadata in PSRAM-backed vectors. Each NDJSON database can have a binary `.qidx` cache containing offsets, levels, ID hashes, and category hashes. The cache is accepted only when its format, length, entry CRC32, monotonic offsets, source size and modification time, and four distributed source sample CRC32 values all match. A missing or stale cache causes one sequential metadata scan; the firmware then writes and verifies a temporary cache before replacing the old file. `AdaptiveCurriculum` builds its level/category pools directly from this metadata and reads an NDJSON record only when it is actually selecting or resolving a question. Mounted SD cards can be prepared ahead of time with `python scripts/gen_tutor_index.py --db-dir <drive>:\kids_tutor\db`.

Realtime defers proactive battery, idle, touch, and proximity responses while a camera or music function is pending and while another response is in flight. This prevents a second response from reclaiming the single I2S peripheral during shutter playback or camera DMA setup.

Realtime PCM output uses virtual speaker channel 0 and M5Unified's two queue
slots. Playback begins after the first two fragments have been decoded and both
slots can be queued back-to-back. Two persistent decode buffers then alternate
only after a slot becomes free, so the next audio delta is queued before the
current one ends. A single-fragment response is flushed by `response.done`, which
drains the final slots before speaker I2S is released; disconnect aborts log the
incomplete response. Per-response diagnostics include chunk/byte counts, queue
wait totals, underruns, enqueue failures, drain time, and mid-answer WebSocket
termination.

OpenAI Realtime TLS and Tailscale Funnel TLS share the embedded official ISRG
Root X1/X2 trust bundle. The OpenAI endpoint currently chains through Let's
Encrypt, so the older GTS-only root is not used; hostname and CA validation stay
enabled on both connections.

CoreS3 startup schedules NTP with `configTime()` and polls completion from the
main loop without blocking setup. The optional boot MP3 runs in a low-priority
task through the shared audio mutex. Once the face and active mod are ready the
UI reports `연결 중 · 터치하면 대기`; a touch before `session.updated` remains
queued and starts recording after the session becomes ready. Boot-stage elapsed
times and `ui_ready_ms` are written to Serial. The first power diagnostic also
records PMIC type, battery/VBUS voltage, battery current, and charge state;
off-state USB-only startup is treated as a battery/PMIC/power-key-path problem
because application code cannot execute until those rails are already on.

The user deployment target is `m5stack-cores3-realtime-camera`. It defines
`ENABLE_CAMERA`, registers `take_photo`, and retains the existing countdown,
shutter, capture, preview, save/discard flow. `m5stack-cores3-realtime` remains a
non-camera diagnostic build. Every boot logs whether camera/tool support is
compiled in so an accidental non-camera flash is immediately visible.

## Realtime API Function Calling

### OpenClaw Agent Bridge

`agent_task` delegates diary, PC-side long-term memory, multi-source search, and shopping comparison requests to a LAN PC without replacing the selected LLM. ChatGPT Realtime also delegates `web_search` through the same worker so Brave TLS and JSON processing never run on its WebSocket task. Robot-local functions such as movement, lights, camera, music, study, and timers remain in `FunctionCall`.

- Configuration and the Bridge authentication key are read from SD `/yaml/SC_SecConfig.yaml` under `agentBridge`; the OpenClaw token is never stored on the device.
- `StackchanExConfig` bypasses the upstream raw-secret dump and logs only whether secret fields are configured.
- With `agentBridge.tls` omitted or false, the request remains `POST http://<host>:<port>/v1/agent`. With `tls: true`, it is `POST https://<host>:<port>/v1/agent` through `WiFiClientSecure`; the embedded Let’s Encrypt ISRG Root X1/X2 roots validate both the certificate chain and hostname, and insecure TLS is never enabled. Both modes send `X-Stackchan-Key` and JSON `profile`, `device_id`, `action`, and `text`; success is HTTP 200 with a `text` field.
- RealtimeChatGPT starts a low-priority single-flight worker for both `agent_task` and `web_search`, then returns its result through the existing deferred function-call path. The WebSocket task continues processing while HTTP is pending. Disconnect or cancellation advances the request generation so a late result is discarded.
- Non-Realtime ChatGPT keeps direct Brave Search for `web_search`; its `agent_task` and Gemini's `agent_task` use the Bridge client synchronously. Connect and response timeouts are 5 and 60 seconds respectively.
- A Realtime Bridge failure is returned to the model without falling back to Brave HTTPS on the WebSocket task.
- Request arguments, response bodies, diary/memory content, and authentication values are not written to Serial.
- DNS, TLS, connection, authentication, and timeout failures have distinct redacted diagnostics. The worker logs its stack high-water mark on completion so the 12 KB allocation can be adjusted only from measured evidence.
- LAN deployments use `tls: false` and typically port 8765. Tailnet-external devices may use a public Tailscale Funnel hostname with `tls: true` and port 443 while the PC Bridge remains bound to `127.0.0.1:8765`.
- On CoreS3 Realtime builds, AgentBridge's concurrent Funnel connection uses a hybrid mbedTLS allocator: allocations below 4 KiB stay in internal RAM, while larger TLS records prefer PSRAM and fall back to internal RAM only if necessary. This preserves CA validation while avoiding `mbedtls_ssl_setup()` allocation failures with the OpenAI WebSocket active. Redacted capacity and allocator-placement counters are logged around each HTTPS request.

### Avatar Expression

Realtime API ビルドでは、Function Calling により AI が会話中の感情に合わせて Avatar の表情を変更できる。

- 対象ビルド
  - `REALTIME_API` が定義される PlatformIO 環境。
- 関数名
  - `set_avatar_expression`
- 引数
  - `expression`: `neutral`, `happy`, `angry`, `sad`, `doubt`, `sleepy`
- 実装
  - `src/llm/ChatGPT/FunctionCall.cpp`
  - `m5avatar::Expression` に変換して `Avatar::setExpression()` を呼び出す。
  - `LLMBase.cpp` の `systemRole_realtimeAvatarExpression` で Realtime API の system instructions に利用方針を明示する。
- スコープ
  - `json_Functions` は通常 ChatGPT や Gemini Live からも参照されるため、この関数の schema と実行処理は `#if defined(REALTIME_API)` で限定する。
  - `systemRole_realtimeAvatarExpression` は Realtime 系 LLM の `load_role()` で `systemRole_memory` または `systemRole_noMemory` に追加する。

## Web App
WebAPI.cpp のインラインアセンブラ(マクロ：IMPORT_FILE)で incbinフォルダ内のhtmlファイルやjsファイルをプログラム領域に埋め込む。

### Personalize page
- ファイル構成
  - incbin/personalize.html
  - incbin/personalize.js
- 言語
  - ページやダイアログ内の言語は英語版のみ。
- 画面構成
  - Role (Custom Instructions)

#### Role (Custom Instructions)
- 構成
  - フォーム
    - ロール（カスタム指示）の入出力。
  - 設定ボタン
    - API /role_set をPOSTし、フォームの内容を設定する。
- 画面更新時の動作
  - API /role_get をPOSTし、現在設定されているカスタム指示を取得してフォームに表示する。 

#### Memory
- 構成
  - フォーム
    - 取得した記憶内容を表示。
  - Clearボタン
    - API /memory_clear をPOSTし、記憶内容を消去する。
    - 消去を実行する前に、OK/Cancelのダイアログを表示して本当に消去してよいかを確認する。
- 画面更新時の動作
  - API /memory_get をPOSTし、記憶内容を取得してフォームに表示する。
 
