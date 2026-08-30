#if defined(REALTIME_API)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "share/Mutex.h"
//#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCA/rootCACertificate.h"
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "RealtimeChatGPT.h"
#include "FunctionCall.h"
#include "MCPClient.h"
#include "Robot.h"
#include "DiagLog.h"
#include "CameraAction.h"
#include "MusicAction.h"

#include <base64.h>
#include <esp_heap_caps.h>
#include "libb64/cdecode.h"
#include <WebSocketsClient.h>

using namespace m5avatar;
extern Avatar avatar;

// Retain the model used by the last known-working device build.
#define OPENAI_REALTIME_MODEL "gpt-realtime"

static const char session_update[] =
      "{"
        "\"type\": \"session.update\","
        "\"session\": {"
          "\"type\": \"realtime\","
          "\"model\": \"" OPENAI_REALTIME_MODEL "\","
#ifdef REALTIME_API_WITH_TTS
          "\"output_modalities\": [\"text\"],"
#else
          "\"output_modalities\": [\"audio\"],"
#endif
          "\"audio\": {"
            "\"input\": {"
              "\"format\": {"
                "\"type\": \"audio/pcm\","
                "\"rate\": 24000"
              "},"
              "\"turn_detection\": {"
                "\"type\": \"server_vad\","
                "\"threshold\": 0.35,"
                "\"prefix_padding_ms\": 300,"
                "\"silence_duration_ms\": 700,"
                "\"create_response\": true,"
                "\"interrupt_response\": false"
              "}"
            "},"
            "\"output\": {"
              "\"format\": {"
                "\"type\": \"audio/pcm\","
                "\"rate\": 24000"
              "},"
              //"\"voice\": \"sage\""
              "\"voice\": \"marin\""
            "}"
          "},"
          "\"instructions\": \"You are a friendly robot named 로니. Respond only in Korean.\","
          "\"max_output_tokens\": 2500,"
          "\"tools\":[]"
        "}"
      "}";


static const char input_audio_append[] =
        "{"
          "\"type\": \"input_audio_buffer.append\","
          "\"audio\": \"REPLACE_TO_AUDIO_BASE64\""
        "}";

// for function calling
//
static const char conversation_item_create[] =
        "{"
            "\"type\": \"conversation.item.create\","
            "\"item\": {"
                "\"type\": \"function_call_output\","
                "\"call_id\": \"REPLACE_TO_CALL_ID\","
                "\"output\": \"{\\\"result\\\":\\\"REPLACE_TO_OUTPUT\\\"}\""
            "}"
        "}";

static const char response_create[] =
        "{"
            "\"type\": \"response.create\""
        "}";

// WebSocketのコールバック関数としてクラスメソッドを渡せないので、コールバック関数を
// 通常の関数にして静的変数を経由してクラスのthisポインタを渡す。
static RealtimeChatGPT* p_this;

// Watchdog: heartbeat catches dead TCP/PING but the server occasionally accepts
// user audio, sends back PING/PONG fine, yet never produces response.done
// (more frequent on gpt-realtime full model once context grows past ~3k tokens).
// If we recorded a user commit but never got response.done within the timeout,
// force-disconnect → setReconnectInterval kicks in → fresh session.
// NOTE: the actual timeout check + disconnect now runs inside onWebSocketTick()
// on the WebSocket task — it must NOT be done from a separate task, since calling
// webSocket.disconnect() concurrently with webSocket.loop() corrupts the TLS
// stream ("SSL MAC verification failed" → null-PC crash in the ws_wdt task).
static volatile uint32_t last_commit_time = 0;
static const uint32_t RESPONSE_TIMEOUT_MS = 35000;
static const uint32_t SESSION_READY_TIMEOUT_MS = 15000;

// Mic↔Speaker I2S ownership for the current response. Must only be taken when
// we are about to play (first output_audio.delta / proactive), NOT on
// input_audio_buffer.committed — ending the mic inside the WS callback races
// the TLS stack on CoreS3 and drops the socket right after VAD commit.
#ifndef REALTIME_API_WITH_TTS
static bool audio_i2s_held = false;

static void realtime_claim_speaker_i2s() {
    if (audio_i2s_held) return;
    enterMutexAudio();
    if (M5.Mic.isEnabled()) M5.Mic.end();
    vTaskDelay(pdMS_TO_TICKS(40));
    if (!M5.Speaker.begin()) {
        Serial.println("[WSc] Speaker.begin failed");
        vTaskDelay(pdMS_TO_TICKS(40));
        M5.Mic.begin();
        exitMutexAudio();
        return;
    }
    audio_i2s_held = true;
}

static void realtime_release_speaker_i2s(bool beginMic) {
    if (!audio_i2s_held) return;
    while (M5.Speaker.isPlaying()) { vTaskDelay(1); }
    M5.Speaker.end();
    vTaskDelay(pdMS_TO_TICKS(40));
    if (beginMic && !M5.Mic.begin()) {
        Serial.println("[WSc] Mic.begin failed after speaker release");
    }
    exitMutexAudio();
    audio_i2s_held = false;
}
#endif

static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    String msgType, delta;
    DeserializationError error;

	switch(type) {
		case WStype_DISCONNECTED:
			Serial.printf("[WSc] Disconnected!\n");
			g_ws_connected = false;
			p_this->sessionUpdateSentAt = 0;
			p_this->setRealtimeSessionReady(false);
			Serial.printf("[WSc] state wifi=%d rssi=%d heapI=%u largestI=%u heapS=%u\n",
			              (int)WiFi.status(),
			              WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
			              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
			              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
			diag_log("WS disconnected (speaking=%d)", p_this->isSpeaking() ? 1 : 0);
			// If we disconnect MID-OUTPUT (speaking==true) the mutexAudio + speaker
			// were claimed by the committed/proactive handoff and would normally be
			// released at response.done. A disconnect skips that → mutexAudio (a
			// non-recursive mutex) stays locked → the next turn's enterMutexAudio()
			// on this same task deadlocks = silent hang ("듣는 중" frozen, no panic).
			// Clean the audio state here so every reconnect starts fresh.
#ifndef REALTIME_API_WITH_TTS
			if (p_this->isSpeaking() || audio_i2s_held) {
				p_this->setRealtimeSpeaking(false);
				if (audio_i2s_held) {
					M5.Speaker.end();
					M5.Mic.begin();
					exitMutexAudio();
					audio_i2s_held = false;
					Serial.println("[WSc] disconnect mid-speech — audio state reset (mutex released)");
				} else {
					Serial.println("[WSc] disconnect while awaiting response — speaking cleared");
				}
			}
#else
			if (p_this->isSpeaking()) {
				p_this->setRealtimeSpeaking(false);
			}
#endif
			last_commit_time = 0;
            if (p_this->hasDeferredFunctionCall) {
                camera_action_abandon();
                music_action_abandon();
                p_this->hasDeferredFunctionCall = false;
                p_this->deferredFunctionCallId = "";
                Serial.println("[camera-action] deferred call abandoned on disconnect");
            }
			break;
		case WStype_CONNECTED:
			Serial.printf("[WSc] Connected to url: %s\n", payload);
			g_ws_connected = true;
			p_this->setRealtimeSessionReady(false);
			Serial.printf("[WSc] state wifi=%d rssi=%d heapI=%u largestI=%u heapS=%u\n",
			              (int)WiFi.status(),
			              WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0,
			              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
			              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
			diag_log("WS connected");

            /*
             * session.updateでAPIの振る舞いをカスタマイズする
             */
            {
                SpiRamJsonDocument sessionUpdateDoc(1024*10);
                DeserializationError error = deserializeJson(sessionUpdateDoc, session_update);
                if (error) {
                    Serial.println("webSocketEvent: JSON deserialization error (session_update)");
                }

                // instructionsにロール、前回会話の要約を設定
                //
                // Do not dump full role/sysRole to Serial — USB-CDC blocks the
                // WebSocket task for a long time and starves TLS during reconnect.
                Serial.printf("role/sysRole/userInfo chars: %u/%u/%u\n",
                              (unsigned)p_this->role.length(),
                              (unsigned)p_this->systemRole.length(),
                              (unsigned)p_this->userInfo.length());
                sessionUpdateDoc["session"]["instructions"] = p_this->role + " "
                                                              + p_this->systemRole + " "
                                                              + p_this->userInfo;

                // MCP tools listをfunctionとして挿入
                //
                for(int s=0; s < p_this->param.llm_conf.nMcpServers; s++){
                    if(true == p_this->param.llm_conf.mcpServer[s].disabled){
                        continue;
                    }
                    if(!p_this->mcpClient[s]->isConnected()){
                        continue;
                    }

                    for(int t=0; t < p_this->mcpClient[s]->nTools; t++){
                        sessionUpdateDoc["session"]["tools"].add(p_this->mcpClient[s]->toolsListDoc["result"]["tools"][t]);
                        sessionUpdateDoc["session"]["tools"][t]["type"] = "function";
                    }
                }

                // FunctionCall.cppで定義したfunctionをsession.updateに挿入
                //
                SpiRamJsonDocument functionsDoc(1024*10);
                error = deserializeJson(functionsDoc, json_Functions.c_str());
                if (error) {
                    Serial.println("FunctionCall: JSON deserialization error");
                }

                int nFuncs = functionsDoc.size();
                int nMcpFuncs = sessionUpdateDoc["session"]["tools"].size();
                for(int i=0; i<nFuncs; i++){
                    sessionUpdateDoc["session"]["tools"].add(functionsDoc[i]);
                    sessionUpdateDoc["session"]["tools"][nMcpFuncs + i]["type"] = "function";
                }

                String sessionUpdateStr;
                serializeJson(sessionUpdateDoc, sessionUpdateStr);
                // Do NOT pretty-print the full tools JSON to Serial — USB-CDC blocks
                // the WebSocket task for seconds and the UI looks frozen (same class of
                // bug as per-chunk audio.delta logging).
                int nTools = sessionUpdateDoc["session"]["tools"].size();
                Serial.printf("[WSc] session.update sent (%u bytes, tools=%d)\n",
                              (unsigned)sessionUpdateStr.length(), nTools);
				if (p_this->sendTextChecked("session_update", sessionUpdateStr)) {
					p_this->sessionUpdateSentAt = millis();
				} else {
					p_this->sessionUpdateSentAt = 0;
				}
            }
			break;
		case WStype_TEXT:
			// Per-message logging removed: these fired on EVERY audio.delta (~25-50/sec)
			// and blocked the WebSocket task on USB-CDC writes → choppy audio.
			//Serial.printf("[WSc] get text: %s\n", payload);
			//Serial.printf("[WSc] text size: %d\n", strlen((char*)payload));

            error = deserializeJson(p_this->msgDoc, payload);
            if (error) {
                Serial.printf("WebSocket Event: JSON deserialization error %d\n", error.code());
            }

            msgType = p_this->msgDoc["type"].as<String>();
            //Serial.printf("[WSc] text type: %s\n", msgType.c_str());

            if(msgType.equals("session.updated")){
                Serial.println("[WSc] session.updated — ready (tap to talk)");
                p_this->sessionUpdateSentAt = 0;
                p_this->setRealtimeSessionReady(true);
                avatar.setSpeechText(p_this->isRealtimeRecording() ? "듣는 중..." : "터치해서 시작");
            }
            else if(msgType.equals("input_audio_buffer.speech_started")){
                Serial.println("[WSc] speech_started");
                p_this->resetRealtimeRecordStartTime();
                // Prefer draining the socket over immediately capturing the next
                // chunk — speech_started often arrives with follow-up events and
                // a back-to-back 5–8 KB append was dropping CoreS3 TLS.
                for (int i = 0; i < 5; i++) {
                    p_this->webSocket.loop();
                    delay(1);
                }
            }
            else if(msgType.equals("input_audio_buffer.speech_stopped")){
                Serial.println("[WSc] speech_stopped");
            }
            else if(msgType.equals("input_audio_buffer.committed")){
                Serial.printf("[WSc] input audio committed\n");
                last_commit_time = millis();
                p_this->pauseRealtimeRecord(true);
                // Mark speaking so listen does not resume, but do NOT touch I2S
                // here — Mic.end/Speaker.begin on this callback drops CoreS3 TLS.
                p_this->setRealtimeSpeaking(true);
            }
#ifndef REALTIME_API_WITH_TTS            
            else if(msgType.equals("response.output_audio_transcript.delta")){
                delta = p_this->msgDoc["delta"].as<String>();
                // transcript delta log silenced (was per-chunk → blocked task during long speech)
                //Serial.printf("[WSc] delta: %s\n", delta.c_str());
            }
            else if(msgType.equals("response.output_audio.delta")){
                // First audio chunk of this response claims the shared I2S bus.
                if (!audio_i2s_held) {
                    realtime_claim_speaker_i2s();
                    Serial.println("[WSc] speaker I2S claimed for output");
                }
                if (!p_this->isSpeaking()) {
                    p_this->setRealtimeSpeaking(true);
                }
                delta = p_this->msgDoc["delta"].as<String>();
                p_this->streamAudioDelta(delta);
            }
#else
            else if(msgType.equals("response.output_text.delta")){
                p_this->outputText += p_this->msgDoc["delta"].as<String>();

                // 区切り文字を検出したらテキストをキューに追加
                int idx = p_this->search_delimiter(p_this->outputText);
                if(idx > 0){
                    String inputText = p_this->outputText.substring(0, idx);
                    Serial.printf("[WSc] Push text: %s\n", inputText.c_str());
                    p_this->outputTextQueue.push_back(inputText);
                    p_this->outputText = p_this->outputText.substring(idx + strlen("。"), p_this->outputText.length());
                }
            }
#endif
            else if(msgType.equals("response.done")){
                last_commit_time = 0;
                // Avoid dumping the full JSON — USB-CDC floods stall the UI loop.
                int outputNum = p_this->msgDoc["response"]["output"].size();
                Serial.printf("[WSc] response.done outputs=%d DMA=%u SPIRAM=%u INT=%u\n",
                    outputNum,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                bool isFuncCall = false;
                bool modeSwitchRequested = false;
                bool deferredActionRequested = false;
                for(int i = 0; i < outputNum; i++){
                    String outputType = p_this->msgDoc["response"]["output"][i]["type"].as<String>();
                    if(outputType.equals("function_call")){
                        //Serial.printf("[WSc] function call payload: %s\n", payload);
                        isFuncCall = true;
                        const char* name = p_this->msgDoc["response"]["output"][i]["name"];
                        const char* args = p_this->msgDoc["response"]["output"][i]["arguments"];
                        const char* call_id = p_this->msgDoc["response"]["output"][i]["call_id"];
                        Serial.printf("name: %s, args: %s\n", name, args);

                        //avatar.setSpeechFont(&fonts::efontJA_12);
                        //avatar.setSpeechText(name);
                        String response = p_this->fnCall->exec_calledFunc(name, args);
                        modeSwitchRequested = p_this->fnCall->consumeModeSwitchRequest()
                                              || modeSwitchRequested;
                        bool deferred = p_this->fnCall->consumeDeferredActionRequest();
                        if (deferred) {
                            p_this->deferredFunctionCallId = call_id ? call_id : "";
                            p_this->hasDeferredFunctionCall = true;
                            deferredActionRequested = true;
                            Serial.printf("[WSc] function deferred: %s\n", name ? name : "");
                            continue;
                        }
                        response.replace("\"", "\\\"");     //JSON内の文字列を囲む"にエスケープ(\)を付ける

                        String json(conversation_item_create);
                        json.replace("REPLACE_TO_CALL_ID", call_id);
                        json.replace("REPLACE_TO_OUTPUT", response.c_str());
                        Serial.printf("[WSc] function output: %s\n", json.c_str());
                        p_this->sendTextChecked("function_output", json);
                    }
                }

                if (modeSwitchRequested && deferredActionRequested) {
                    camera_action_abandon();
                    music_action_abandon();
                    String response = "{\\\"error\\\":\\\"mode switch cancelled the camera request\\\"}";
                    String json(conversation_item_create);
                    json.replace("REPLACE_TO_CALL_ID", p_this->deferredFunctionCallId);
                    json.replace("REPLACE_TO_OUTPUT", response);
                    p_this->sendTextChecked("cancelled_function_output", json);
                    p_this->hasDeferredFunctionCall = false;
                    p_this->deferredFunctionCallId = "";
                    deferredActionRequested = false;
                }

                if(deferredActionRequested && !modeSwitchRequested){
                    // A deferred device action includes countdown and human UI.
                    // Do not apply the model-response watchdog while waiting for it;
                    // onWebSocketTick() re-arms the watchdog after sending the result.
                    last_commit_time = 0;
                    // Free I2S DMA before loop-task camera init. Skipping this
                    // left Speaker/Mic buffers in internal RAM so ll_cam's
                    // 15,360-byte DMA malloc failed (largest block ~4 KB).
                    if (p_this->isRealtimeRecording()) p_this->pauseRealtimeRecord(true);
#ifndef REALTIME_API_WITH_TTS
                    realtime_release_speaker_i2s(false);
                    M5.Mic.end();
                    if (p_this->isSpeaking()) {
                        p_this->setRealtimeSpeaking(false, false);
                    }
#else
                    while (M5.Speaker.isPlaying()) { vTaskDelay(1); }
                    M5.Speaker.end();
                    M5.Mic.end();
#endif
                    Serial.printf("[WSc] deferred audio released DMA largest=%u\n",
                                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
                    Serial.println("[WSc] waiting for deferred function result");
                } else if(isFuncCall && !modeSwitchRequested){
                    // All function outputs must be present before requesting the
                    // model's follow-up. Keep the audio handoff until that response
                    // finishes, then the non-function branch below resumes listening.
                    p_this->sendTextChecked("function_response_create", response_create);
                    Serial.println("[WSc] function_call done (speaking held for follow-up)");
                } else {
#ifndef REALTIME_API_WITH_TTS
                    // Release speaker I2S only if this turn claimed it. Function-only
                    // turns may complete with speaking=true but audio_i2s_held=false.
                    realtime_release_speaker_i2s(!modeSwitchRequested);

                    // A normal turn remains inside the conversation session: the
                    // first tap starts it, each response resumes listening, and the
                    // existing 30-second recorder timeout returns to tap-to-start.
                    // A study mode switch must leave the mic idle for RealtimeAiMod::pause().
                    if (!modeSwitchRequested) {
                        p_this->startRealtimeRecord();
                    } else {
                        p_this->stopRealtimeRecord();
                    }

                    for(int i=0; i<2; i++){
                        memset(p_this->audioBuf[i], 0, 100 * 1024);
                    }
                    p_this->setRealtimeSpeaking(false, !modeSwitchRequested);
#else
                    if (modeSwitchRequested) {
                        p_this->response_done = false;
                        p_this->stopRealtimeRecord();
                        p_this->setRealtimeSpeaking(false, false);
                    } else {
                        p_this->response_done = true;
                    }
#endif
                    if (modeSwitchRequested) {
                        Serial.println("[WSc] study mode switch: audio released, follow-up skipped");
                    }
                }
            }
            else if(msgType.equals("rate_limits.updated")){
                //Serial.printf("[WSc] payload: %s\n", payload);
            }
            else if(msgType.equals("error")){
                Serial.printf("[WSc] payload: %s\n", payload);
            }
            else {
                Serial.printf("[WSc] type=%s len=%u\n", msgType.c_str(), (unsigned)length);
            }

			break;
		case WStype_BIN:
			Serial.printf("[WSc] get binary length: %u\n", length);
			p_this->hexdump(payload, length);
			break;
		case WStype_ERROR:
			if (length >= 2) {
				uint16_t closeCode = ((uint16_t)payload[0] << 8) | payload[1];
				Serial.printf("[WSc] error/close len=%u code=%u reason=%.*s\n",
				              (unsigned)length, (unsigned)closeCode,
				              (int)(length - 2), (const char*)(payload + 2));
			} else {
				Serial.printf("[WSc] error/close len=%u\n", (unsigned)length);
			}
			break;
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
 			Serial.printf("[WSc] payload: %s\n", payload);		
            break;
		case WStype_PING:
		case WStype_PONG:
			// WebSocket keepalive frames. The library handles the reply; they are
			// not Realtime API events and must not be reported as unknown errors.
			break;
        default:
			Serial.printf("[WSc] Unknown event\n");
            //Serial.printf("[WSc] payload: %s\n", payload);
            break;
	}

}


RealtimeChatGPT::RealtimeChatGPT(llm_param_t param)
  // Match the last known-good GitHub realtime path: 16 kHz / 2000 samples,
  // no resample. Session still declares 24000 (API minimum); that mismatch is
  // what origin/main shipped and the user confirmed worked yesterday.
  : RealtimeLLMBase(param, 16000, 2000, 16000),
    role(""),
    userInfo("User Info: "),
    systemRole("")
{
  p_this = this;    //コールバック関数に静的変数経由でthisポインタを渡す
  msgDoc = SpiRamJsonDocument(1024*150);
  
  initMcpClientList(mcpClient, param.llm_conf.mcpServer, param.llm_conf.nMcpServers);
  fnCall = new FunctionCall(param, this, mcpClient);
  //fnCall->init_func_call_settings(robot->m_config);

  if(proactiveMux == NULL) proactiveMux = xSemaphoreCreateMutex();
  if(reconnectMux == NULL) reconnectMux = xSemaphoreCreateMutex();

  enableMemory(param.llm_conf.enableMemory);
  if(enableMemory()){
    Serial.println("Memory is enabled");
    M5.Lcd.println("Memory is enabled");
  }
  load_role();


  // WebSocket connect
  //
  avatar.setSpeechText("연결 중...");
  webSocket.beginSslWithCA("api.openai.com", 443,
                           "/v1/realtime?model=" OPENAI_REALTIME_MODEL,
                           root_ca_openai);

  // event handler
  p_this = this;    //コールバック関数に静的変数経由でthisポインタを渡す
  webSocket.onEvent(webSocketEvent);
  String auth = "Bearer " + param.api_key;
  webSocket.setAuthorization(auth.c_str());

  // Keep-alive only — do not auto-disconnect on missed pongs. During mic
  // capture CoreS3 often delays WS loop enough for false HB timeouts that
  // drop a live listen right after the first audio_append.
  webSocket.enableHeartbeat(20000, 15000, 0);
  webSocket.setReconnectInterval(5000);

  // Response-timeout watchdog now runs inside onWebSocketTick() on the WebSocket
  // task (no separate task touching webSocket — see note above last_commit_time).

  // try ever 5000 again if connection has failed
  webSocket.setReconnectInterval(5000);

}


void RealtimeChatGPT::load_role(){
  Serial.println("Load role from SPIFFS.");
  if(enableMemory()){
    systemRole = systemRole_memory;
  }else{
    systemRole = systemRole_noMemory;
  }
  systemRole += " " + systemRole_realtimeAvatarExpression;

  if(load_system_prompt_from_spiffs()){
    role = String((const char*)systemPrompt["messages"][SYSTEM_PROMPT_INDEX_USER_ROLE]["content"]);
    //Serial.printf("role length: %d\n", role.length());
    if (role == "") {
      Serial.println("SPIFFS user role is empty. set default role.");
      role = defaultRole;
    }

    userInfo = String((const char*)systemPrompt["messages"][SYSTEM_PROMPT_INDEX_USER_INFO]["content"]);
    //Serial.println(userInfo);
    int idx = userInfo.indexOf("User Info");
    if(idx < 0 || !enableMemory()){
      userInfo = "User Info: ";
    }
  }else{
    // load_system_prompt_from_spiffs()内でSPIFFSからの取得失敗かつ
    // デフォルトのシステムプロンプト設定に失敗した場合（通常起こり得ない）。
    role = defaultRole;
    userInfo = "User Info: ";
  }
}

String& RealtimeChatGPT::buildInputAudioJson(String& jsonBuf, String& base64)
{
    jsonBuf.concat(input_audio_append);
    jsonBuf.replace("REPLACE_TO_AUDIO_BASE64", base64);
    //Serial.println(jsonBuf);
    return jsonBuf;
}

void RealtimeChatGPT::audioAppendEnvelope(const char*& prefix, const char*& suffix)
{
    prefix = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
    suffix = "\"}";
}

bool RealtimeChatGPT::sendTextChecked(const char* label, const String& payload)
{
  return sendTextChecked(label, payload.c_str(), payload.length());
}

bool RealtimeChatGPT::sendTextChecked(const char* label, const char* payload, size_t length)
{
  bool sent = webSocket.sendTXT(payload, length);
  static uint8_t audioFailStreak = 0;
  const bool isAudio = label && strcmp(label, "audio_append") == 0;

  // Large or mid-listen TLS writes often need a couple of loop/yield cycles
  // before the TCP send buffer accepts the next frame.
  if (!sent && isAudio) {
    for (int attempt = 0; attempt < 4 && !sent; attempt++) {
      webSocket.loop();
      delay(5);
      sent = webSocket.sendTXT(payload, length);
    }
  }

  if (!sent) {
    const char* safeLabel = label ? label : "unknown";
    if (isAudio) {
      audioFailStreak++;
      Serial.printf("[WSc] send failed label=%s streak=%u — drop chunk\n",
                    safeLabel, (unsigned)audioFailStreak);
      if (audioFailStreak >= 8) {
        Serial.println("[WSc] audio_append fail streak — reconnect queued");
        diag_log("WS send failed label=%s streak=%u", safeLabel, (unsigned)audioFailStreak);
        setRealtimeSessionReady(false);
        queueReconnect("audio_append");
        audioFailStreak = 0;
      }
    } else {
      audioFailStreak = 0;
      Serial.printf("[WSc] send failed label=%s — reconnect queued\n", safeLabel);
      diag_log("WS send failed label=%s", safeLabel);
      setRealtimeSessionReady(false);
      queueReconnect(safeLabel);
    }
  } else if (isAudio) {
    audioFailStreak = 0;
  }
  return sent;
}


// Proactive speech: queue a user-role message; onWebSocketTick() (WS task) sends it
// and triggers a response. Used by scheduled greetings, idle talk, pet/touch/battery/
// proximity reactions and camera vision — all of which run on the loop/HTTP/scheduler
// task, NOT the WebSocket task. We therefore only ENQUEUE here: calling sendTXT from a
// different task than the one running webSocket.loop() corrupts the TLS stream.
void RealtimeChatGPT::pushUserText(const String& text) {
  if (proactiveMux == NULL) return;   // not constructed yet
  xSemaphoreTake(proactiveMux, portMAX_DELAY);
  if (hasPendingProactive) {
    // A proactive utterance is already queued and not yet sent — coalesce by
    // dropping the newer one rather than growing an unbounded backlog.
    Serial.printf("[realtime] proactive busy, dropping: %s\n", text.c_str());
  } else {
    pendingProactiveText = text;
    hasPendingProactive = true;
  }
  xSemaphoreGive(proactiveMux);
}

// Request a session reconnect (handled on the WS task in onWebSocketTick).
void RealtimeChatGPT::requestReconnect() {
  queueReconnect("persona_update");
}

void RealtimeChatGPT::queueReconnect(const char* reason) {
  if (reconnectMux) xSemaphoreTake(reconnectMux, portMAX_DELAY);
  if (!reconnectRequest) {
    reconnectReason = reason ? reason : "unspecified";
    reconnectRequest = true;
  }
  if (reconnectMux) xSemaphoreGive(reconnectMux);
}

// Runs on the WebSocket task each iteration (after webSocket.loop()). This is the only
// place outside the event callback that may touch webSocket, so both the response
// watchdog and the proactive-send live here — never on another task.
void RealtimeChatGPT::onWebSocketTick() {
  String reconnectNow;
  if (reconnectMux) xSemaphoreTake(reconnectMux, portMAX_DELAY);
  if (reconnectRequest) {
    reconnectNow = reconnectReason;
    reconnectReason = "";
    reconnectRequest = false;
  }
  if (reconnectMux) xSemaphoreGive(reconnectMux);
  if (reconnectNow.length()) {
    last_commit_time = 0;
    sessionUpdateSentAt = 0;
    Serial.printf("[WSc] reconnecting reason=%s\n", reconnectNow.c_str());
    diag_log("WS reconnect reason=%s", reconnectNow.c_str());
    webSocket.disconnect();
    return;
  }

  if (sessionUpdateSentAt != 0
      && (uint32_t)(millis() - sessionUpdateSentAt) >= SESSION_READY_TIMEOUT_MS) {
    Serial.printf("[WSc] session.updated timeout (%ums) — reconnect queued\n",
                  (unsigned)(millis() - sessionUpdateSentAt));
    diag_log("session.updated timeout %ums", (unsigned)(millis() - sessionUpdateSentAt));
    sessionUpdateSentAt = 0;
    setRealtimeSessionReady(false);
    queueReconnect("session_ready_timeout");
    return;
  }

  // Finish loop-task camera work from the WebSocket task so sendTXT is never
  // called concurrently with webSocket.loop().
  if (hasDeferredFunctionCall) {
    String result;
    if (camera_action_take_result(result) || music_action_take_result(result)) {
      result.replace("\"", "\\\"");
      String json(conversation_item_create);
      json.replace("REPLACE_TO_CALL_ID", deferredFunctionCallId);
      json.replace("REPLACE_TO_OUTPUT", result);
      Serial.printf("[WSc] deferred function output: %s\n", json.c_str());
      if (!sendTextChecked("deferred_function_output", json)) return;
      hasDeferredFunctionCall = false;
      deferredFunctionCallId = "";
      last_commit_time = millis();
      if (!sendTextChecked("deferred_response_create", response_create)) return;
      Serial.println("[WSc] deferred function complete (follow-up requested)");
      return;
    }

    // Countdown/capture/save owns the audio and camera DMA handoff until a
    // function result is ready. A battery/idle/touch proactive queued during
    // this window must not create a second response and reclaim I2S.
    return;
  }

  // (1) Response-timeout watchdog. If a turn/proactive request never produced a
  // response.done within the timeout, force a reconnect for a fresh session.
  uint32_t t = last_commit_time;
  if (t != 0 && (millis() - t) > RESPONSE_TIMEOUT_MS) {
    Serial.printf("[wdt] response timeout (%ums) — reconnect\n", (unsigned)(millis() - t));
    diag_log("wdt response timeout %ums — reconnect", (unsigned)(millis() - t));
    last_commit_time = 0;
    webSocket.disconnect();
    return;
  }

  // There may already be a normal or deferred follow-up response in flight.
  // Keep proactive messages queued until response.done clears the watchdog;
  // otherwise two responses can race for the single speaker I2S peripheral.
  if (t != 0) return;

  // (2) Drain a queued proactive utterance.
  if (!hasPendingProactive) return;

  String text;
  if (proactiveMux) xSemaphoreTake(proactiveMux, portMAX_DELAY);
  text = pendingProactiveText;
  pendingProactiveText = "";
  hasPendingProactive = false;
  if (proactiveMux) xSemaphoreGive(proactiveMux);

  // Only send once the session is live; otherwise the mic→speaker handoff below
  // would never be reversed (no response → mic stuck off → robot goes deaf).
  if (!webSocket.isConnected()) {
    Serial.printf("[realtime] proactive dropped (WS not connected): %s\n", text.c_str());
    return;
  }

  String escaped = text;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");
  String json = "{\"type\":\"conversation.item.create\","
                "\"item\":{\"type\":\"message\",\"role\":\"user\","
                "\"content\":[{\"type\":\"input_text\",\"text\":\"";
  json += escaped;
  json += "\"}]}}";
  if (!sendTextChecked("proactive_item", json)) return;

  // Proactive speech produces NO input_audio_buffer.committed event. Mark
  // speaking so listen stays paused; claim I2S on the first output_audio.delta
  // (same deferred path as VAD commit — avoids TLS drop on CoreS3).
  if (!isSpeaking()) {
    if (isRealtimeRecording()) pauseRealtimeRecord(true);
    setRealtimeSpeaking(true);
  }

  last_commit_time = millis();   // arm the timeout watchdog for this response
  if (!sendTextChecked("proactive_response_create", response_create)) return;
  Serial.printf("[realtime] proactive sent: %s\n", text.c_str());
}


#endif  //REALTIME_API
