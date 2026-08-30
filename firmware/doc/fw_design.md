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

The official CoreS3 Stack-chan profile uses `M5_SCS` servos through the PY32 controller at I2C address `0x6F`. Serial2 uses RX/TX pins 7/6, and servo IDs 1 and 2 control the X/Y axes. CoreS3 GPIO remapping, PWM reattachment, and hard-sweep diagnostics are restricted to the PWM servo type. ESP-NOW movement uses the configured center and limits through `ServoCustom::writeOffset`.

The CoreS3 GC0308 camera uses the board-provided external XCLK (`pin_xclk = -1`) and borrows the already-running `M5.In_I2C` port for SCCB. Camera sessions must not release, reinstall, or restart that I2C port because the same board bus also controls the PY32 servo power path.

Camera capture uses the public `esp_camera_init`, `esp_camera_fb_get`, `esp_camera_fb_return`, and `esp_camera_deinit` lifecycle. Initialization discards two warm-up frames. A missed capture triggers one full driver deinitialization and reinitialization; low-level `cam_take` and `cam_stop`/`cam_start` recovery loops are intentionally avoided.

The camera build links Espressif's precompiled ESP32-S3 `cam_hal` and `ll_cam` objects unchanged, matching the known-good CoreS3 example. Do not recompile them with custom DMA limits, EOF queue depth, task affinity, clock selection, stop/start state changes, or `cam_clk_sel` patches: the custom HAL/LL path produced about 66 EOF interrupts per VSYNC and never completed a frame, while the framework objects capture QVGA RGB565 successfully. Only the GC0308 sensor unit is overridden. GC0308 register reads can time out after returning the stale sensor-ID byte (`0x9B`), so RGB565, QVGA subsampling, and orientation use checked write-only values (`0x24=0xA6`, page-1 `0x53=0x80`/`0x55=0x01`) plus a software shadow for register `0x14`. Page-0 defaults must not be carried into page 1: setting page-1 `0x55` bit 7 stalls the SCCB bus. The PCLK register stays at the framework default.

GC0308 RGB565 frames are big-endian (`RGB565_BE`) and are retained as high-byte/low-byte pairs in the preview buffer. M5GFX therefore consumes that buffer with `setSwapBytes(false)` (`swap565_t` layout); enabling byte swap corrupts only the LCD preview colors. BMP export explicitly reassembles each big-endian pixel before writing BGR888.

Photo countdown/capture holds the servo at its configured origin and rejects queued expression, gaze, web, and ESP-NOW movement until the action ends. Kids Tutor uses the same motion hold while questions are active. After the completion sound finishes, the hold is released and the normal gesture dance is queued; the dance sequence ends at the origin.

Realtime defers proactive battery, idle, touch, and proximity responses while a camera or music function is pending and while another response is in flight. This prevents a second response from reclaiming the single I2S peripheral during shutter playback or camera DMA setup.

## Realtime API Function Calling

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
 
