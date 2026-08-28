#if defined(REALTIME_API)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "share/Mutex.h"
//#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCA/rootCAgoogleGemini.h"
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "RealtimeLLMBase.h"
//#include "FunctionCall.h"
//#include "MCPClient.h"
#include "Robot.h"

#include <base64.h>
#include "libb64/cdecode.h"
#include <WebSocketsClient.h>

using namespace m5avatar;
extern Avatar avatar;

int16_t rtRecBuf[RT_REC_LENGTH];    // リアルタイム録音用メモリ
                                    // Core2だとヒープが不足するので静的な配列とした

TaskHandle_t webSocketLoopTask_h = NULL;

// WebSocketのイベント処理(webSocket.loop())及び、録音データ（約0.1秒）を
// WebSocketで送信するためのループタスク
void webSocketLoopTask(void *arg) {
    Serial.println("WebSocket loop task created");
    RealtimeLLMBase* pThis = (RealtimeLLMBase*)arg;

    while(1){
        pThis->webSocketProcess();
        //delay(1);     //webSocketProcess()内で状態によってスリープ時間を変更
    }
}


RealtimeLLMBase::RealtimeLLMBase(llm_param_t param) : 
    LLMBase(param, 0),
    msgDoc(0),
    rtRecSamplerate(RT_REC_SAMPLE_RATE),
    rtRecLength(RT_REC_LENGTH),
    realtime_recording(false),
    realtime_record_requested(false),
    realtime_session_ready(false),
    response_done(false),
    startTime(0),
    nextBufIdx(0),
    outputText(String(""))
{
#ifdef REALTIME_API_RECORD_TEST
  // リアルタイム録音のチャンクデータを蓄積してテスト再生するためのバッファ（約4s）
  recTestLenMax = rtRecLength * 40;
  recTestLenCnt = 0;
  recTestBuf = (int16_t*)heap_caps_malloc(recTestLenMax * sizeof(*rtRecBuf), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif

#ifndef REALTIME_API_WITH_TTS
  // ストリーミング音声再生用のダブルバッファを初期化
  for(int i=0; i<2; i++){
    audioBuf[i] = (uint8_t*)malloc(100 * 1024);
    memset(audioBuf[i], 0, 100 * 1024);
  }
#endif

}

void RealtimeLLMBase::webSocketProcess()
{
    webSocket.loop();

    // All other webSocket access (proactive sends, timeout-driven disconnect)
    // funnels through here so it stays on this single task — WebSocketsClient /
    // its TLS client are not thread-safe.
    onWebSocketTick();

#ifdef REALTIME_API_WITH_TTS
    if(response_done && !speaking){
        startRealtimeRecord();
        response_done = false;
    }
#endif

    if(realtime_recording){
        enterMutexAudio();
        //M5.Mic.begin();
        if(!M5.Mic.record(rtRecBuf, rtRecLength, rtRecSamplerate)){
            Serial.println("Mic.record() returns false");
            delay(1000);
        }
        //M5.Mic.end();
        exitMutexAudio();
        String audio_base64;
        audio_base64 = base64::encode((u8*)rtRecBuf, rtRecLength * sizeof(int16_t));

#ifdef REALTIME_API_RECORD_TEST
        if((recTestLenCnt + rtRecLength) < recTestLenMax){
            memcpy((u8*)&recTestBuf[recTestLenCnt], (u8*)rtRecBuf, rtRecLength * sizeof(int16_t));
            recTestLenCnt += rtRecLength;
        }
#else
        String audioJsonBuf("");
        String& audioJson = buildInputAudioJson(audioJsonBuf, audio_base64);
        bool sent = webSocket.sendTXT(audioJson);
        // Print one compact confirmation at the start of a turn (and then at
        // most once every five seconds). This proves whether the disconnect is
        // before or after the client hands an audio frame to the TLS socket.
        static uint32_t lastAudioLogMs = 0;
        if (!sent || (millis() - lastAudioLogMs >= 5000)) {
            Serial.printf("[realtime] audio append sent=%d pcm=%u json=%u rate=%d\n",
                          (int)sent,
                          (unsigned)(rtRecLength * sizeof(int16_t)),
                          (unsigned)audioJson.length(),
                          rtRecSamplerate);
            lastAudioLogMs = millis();
        }
        if (!sent) {
            // Preserve the user's listening request so it resumes after the
            // WebSocket library reconnects instead of silently dropping it.
            setRealtimeSessionReady(false);
        }
#endif

        portTickType elapsedTime = checkRealtimeRecordTimeout();

#if 0   //Debug リスニング経過時間の表示
        static char speechTxt[64];
        sprintf(speechTxt, "Listening:%ds", int(elapsedTime / 1000));
        avatar.setSpeechText(speechTxt);
#else
        avatar.setSpeechText("듣는 중...");
#endif
        delay(1);
    }
    else{
        if(speaking){
            //発話中もしくはテキスト生成中
            avatar.setSpeechText("");
            resetRealtimeRecordStartTime(); //長いテキストを発話中にタイムアウトしてしまうのを防ぐ
            delay(1);
        }
        else if(realtime_record_requested && !realtime_session_ready){
            avatar.setSpeechText("연결 중...");
            delay(10);
        }
        else{
            avatar.setSpeechText("터치하면 시작");
            delay(10);
        }
    }
}

int RealtimeLLMBase::getAudioLevel()
{
    return abs(*audioBuf[nextBufIdx ^ 1]) * 50;
}

void RealtimeLLMBase::startRealtimeRecord()
{
    realtime_record_requested = true;
    if(!realtime_session_ready){
        Serial.println("[realtime] listen queued until session ready");
        avatar.setSpeechText("연결 중...");
        return;
    }
    if(!realtime_recording){
        Serial.println("Start realtime recording");
        realtime_recording = true;
        startTime = xTaskGetTickCount();
    }
}

void RealtimeLLMBase::stopRealtimeRecord()
{
    realtime_record_requested = false;
    if(realtime_recording){
        Serial.println("Stop realtime recording");
        realtime_recording = false;
        startTime = 0;
    }
}

void RealtimeLLMBase::setRealtimeSessionReady(bool ready)
{
    realtime_session_ready = ready;

    if(!ready){
        // Audio append frames cannot survive a disconnected socket. Stop the
        // capture loop immediately, but remember the user's listening intent so
        // a later session.updated/setupComplete can resume it automatically.
        if(realtime_recording){
            realtime_recording = false;
            realtime_record_requested = true;
            startTime = 0;
            Serial.println("[realtime] session lost — recording paused, resume queued");
        }
        return;
    }

    if(realtime_record_requested && !realtime_recording && !speaking){
        Serial.println("[realtime] session ready — starting queued listen");
        startRealtimeRecord();
    }
}

void RealtimeLLMBase::resetRealtimeRecordStartTime()
{
    startTime = xTaskGetTickCount();
}

portTickType RealtimeLLMBase::checkRealtimeRecordTimeout()
{
    portTickType elapsedTime;
    elapsedTime = (xTaskGetTickCount() - startTime) * portTICK_RATE_MS;
    if(elapsedTime > REALTIME_RECORD_TIMEOUT){
        Serial.println("Realtime recording timeout");
        stopRealtimeRecord();
#ifdef REALTIME_API_RECORD_TEST
        M5.Mic.end();
        if (M5.Speaker.begin())
        {
            M5.Speaker.playRaw(recTestBuf, recTestLenCnt, rtRecSamplerate);
            while (M5.Speaker.isPlaying()) { delay(10); }
            M5.Speaker.end();
            M5.Mic.begin();
        }
        recTestLenCnt = 0;
#endif
    }

    return elapsedTime;
}

int RealtimeLLMBase::base64_decode(const char* input, int size, char* output)
{
	/* keep track of our decoded position */
	char* c = output;
	/* store the number of bytes decoded by a single call */
	int cnt = 0;
	/* we need a decoder state */
	base64_decodestate s;
	
	/*---------- START DECODING ----------*/
	/* initialise the decoder state */
	base64_init_decodestate(&s);
	/* decode the input data */
	cnt = base64_decode_block(input, strlen(input), c, &s);
	c += cnt;
	/* note: there is no base64_decode_blockend! */
	/*---------- STOP DECODING  ----------*/
	
	/* we want to print the decoded data, so null-terminate it: */
	*c = 0;
	
	return cnt;
}


void RealtimeLLMBase::hexdump(const void *mem, uint32_t len, uint8_t cols) {
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
}


void RealtimeLLMBase::streamAudioDelta(String& delta)
{
    // Per-chunk Serial logging removed: fired on every audio.delta (~25-50/sec)
    // INSIDE the playback path, starving the speaker between base64 decode and
    // playRaw. This was the residual buzz after the WS-task log fix.
    int base64Size = delta.length();
    uint8_t* buf = audioBuf[nextBufIdx];
    int len = base64_decode(delta.c_str(), base64Size, (char*)buf);

    while (M5.Speaker.isPlaying()) { vTaskDelay(1); }
    M5.Speaker.playRaw((int16_t*)buf, len/2, 24000, false);
    nextBufIdx ^= 1;  //ダブルバッファを切り替え
}

void RealtimeLLMBase::invokeWebSocketLoopTask(void)
{
    xTaskCreate(webSocketLoopTask, /* Function to implement the task */
            "webSocketLoopTask", /* Name of the task */
            6*1024,               /* Stack size in words */
            this,                 /* Task input parameter */
            3,                    /* Priority of the task */
            &webSocketLoopTask_h);                /* Task handle. */
}

void RealtimeLLMBase::suspendWebSocketLoopTask(void)
{
    if (eTaskGetState(webSocketLoopTask_h) != eSuspended) {
      Serial.println("webSocketLoopTask Suspend");
      vTaskSuspend(webSocketLoopTask_h);
    }
}

void RealtimeLLMBase::resumeWebSocketLoopTask(void)
{
    if (eTaskGetState(webSocketLoopTask_h) == eSuspended) {
      Serial.println("webSocketLoopTask Resume");
      vTaskResume(webSocketLoopTask_h);
    }
}

#endif  //REALTIME_API
