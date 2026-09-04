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
#include <esp_heap_caps.h>

using namespace m5avatar;
extern Avatar avatar;

int16_t rtRecBuf[RT_REC_BUFFER_SAMPLES];    // リアルタイム録音用メモリ
                                    // Core2だとヒープが不足するので静的な配列とした
// Wire PCM after optional upsample (e.g. 16 kHz capture → 24 kHz session).
static int16_t rtWireBuf[RT_REC_BUFFER_SAMPLES];

// Reused PSRAM JSON buffer for audio append — avoids Arduino String / base64::encode
// churn that fragments internal heap and drops CoreS3 TLS mid-listen.
static char* rtAudioJsonBuf = nullptr;
static const size_t RT_AUDIO_JSON_CAP = 12 * 1024;

static bool ensureAudioJsonBuf() {
    if (rtAudioJsonBuf) return true;
    rtAudioJsonBuf = (char*)heap_caps_malloc(RT_AUDIO_JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rtAudioJsonBuf) {
        rtAudioJsonBuf = (char*)heap_caps_malloc(RT_AUDIO_JSON_CAP, MALLOC_CAP_8BIT);
    }
    if (!rtAudioJsonBuf) {
        Serial.println("[realtime] audio JSON buffer alloc failed");
        return false;
    }
    return true;
}

static size_t pcm16_to_base64(const uint8_t* src, size_t srcLen, char* dst, size_t dstCap) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (dstCap < 1) return 0;
    size_t o = 0;
    for (size_t i = 0; i < srcLen; i += 3) {
        if (o + 4 >= dstCap) return 0;
        uint32_t n = ((uint32_t)src[i]) << 16;
        if (i + 1 < srcLen) n |= ((uint32_t)src[i + 1]) << 8;
        if (i + 2 < srcLen) n |= (uint32_t)src[i + 2];
        dst[o++] = tbl[(n >> 18) & 63];
        dst[o++] = tbl[(n >> 12) & 63];
        dst[o++] = (i + 1 < srcLen) ? tbl[(n >> 6) & 63] : '=';
        dst[o++] = (i + 2 < srcLen) ? tbl[n & 63] : '=';
    }
    dst[o] = '\0';
    return o;
}

// Upsample 16 kHz → 24 kHz (3/2) with linear midpoints. inLen must be even;
// outLen = inLen * 3 / 2 and must fit in RT_REC_BUFFER_SAMPLES.
static int upsample_pcm16_16k_to_24k(const int16_t* in, int inLen, int16_t* out, int outCap) {
    if (inLen < 2 || (inLen & 1) || (inLen * 3 / 2) > outCap) return 0;
    int o = 0;
    for (int i = 0; i < inLen; i += 2) {
        const int16_t a = in[i];
        const int16_t b = in[i + 1];
        out[o++] = a;
        out[o++] = (int16_t)(((int32_t)a + (int32_t)b) / 2);
        out[o++] = b;
    }
    return o;
}

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


RealtimeLLMBase::RealtimeLLMBase(llm_param_t param,
                                 int captureSampleRate,
                                 int captureSamples,
                                 int wireSampleRate) :
    LLMBase(param, 0),
    msgDoc(0),
    rtRecSamplerate(captureSampleRate),
    rtWireSamplerate(wireSampleRate),
    rtRecLength(captureSamples),
    response_done(false),
    startTime(0),
    nextBufIdx(0),
    audioStreamActive(false),
    pendingFirstAudioLen(0),
    audioChunkCount(0),
    audioDecodedBytes(0),
    audioQueueWaitMs(0),
    audioQueueWaitMaxMs(0),
    audioUnderrunCount(0),
    audioEnqueueFailures(0),
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

    if (expireQueuedListen(millis())) {
        Serial.printf("[realtime] queued listen expired after %ums\n",
                      (unsigned)RT_LISTEN_QUEUE_TIMEOUT_MS);
        avatar.setSpeechText("터치하면 시작");
    }

#ifdef REALTIME_API_WITH_TTS
    if(response_done && !isSpeaking()){
        startRealtimeRecord();
        response_done = false;
    }
#endif

    RealtimeStateSnapshot state = getRealtimeStateSnapshot();
    if(state.recording){
        enterMutexAudio();
        if (!M5.Mic.isEnabled()) {
            if (!M5.Mic.begin()) {
                exitMutexAudio();
                Serial.println("[realtime] Mic.begin failed; stop record");
                stopRealtimeRecord();
                delay(100);
                return;
            }
            delay(20);
        }
        if(!M5.Mic.record(rtRecBuf, rtRecLength, rtRecSamplerate)){
            Serial.println("Mic.record() returns false");
            delay(1000);
        }
        //M5.Mic.end();
        exitMutexAudio();

        // A touch or disconnect may arrive while record() is blocked. Drop the
        // captured chunk if it no longer belongs to an active, ready session.
        state = getRealtimeStateSnapshot();
        if (!state.recording || !state.sessionReady || state.speaking) {
            delay(1);
            return;
        }
        if (!webSocket.isConnected()) {
            setRealtimeSessionReady(false);
            delay(1);
            return;
        }
        const int16_t* wirePcm = rtRecBuf;
        int wireSamples = rtRecLength;
        if (rtRecSamplerate == 16000 && rtWireSamplerate == 24000) {
            wireSamples = upsample_pcm16_16k_to_24k(rtRecBuf, rtRecLength,
                                                    rtWireBuf, RT_REC_BUFFER_SAMPLES);
            if (wireSamples <= 0) {
                Serial.println("[realtime] upsample 16k→24k failed");
                delay(1);
                return;
            }
            wirePcm = rtWireBuf;
        } else if (rtRecSamplerate != rtWireSamplerate) {
            Serial.printf("[realtime] unsupported rate pair capture=%d wire=%d\n",
                          rtRecSamplerate, rtWireSamplerate);
        }

#ifdef REALTIME_API_RECORD_TEST
        if((recTestLenCnt + rtRecLength) < recTestLenMax){
            memcpy((u8*)&recTestBuf[recTestLenCnt], (u8*)rtRecBuf, rtRecLength * sizeof(int16_t));
            recTestLenCnt += rtRecLength;
        }
#else
        if (!ensureAudioJsonBuf()) {
            delay(1);
            return;
        }
        const char* prefix = nullptr;
        const char* suffix = nullptr;
        audioAppendEnvelope(prefix, suffix);
        if (!prefix || !suffix) {
            Serial.println("[realtime] audio envelope missing");
            delay(1);
            return;
        }
        const size_t prefixLen = strlen(prefix);
        const size_t suffixLen = strlen(suffix);
        const size_t pcmBytes = (size_t)wireSamples * sizeof(int16_t);
        if (prefixLen + suffixLen + 1 >= RT_AUDIO_JSON_CAP) {
            Serial.println("[realtime] audio JSON cap too small");
            delay(1);
            return;
        }
        memcpy(rtAudioJsonBuf, prefix, prefixLen);
        const size_t b64Len = pcm16_to_base64((const uint8_t*)wirePcm, pcmBytes,
                                              rtAudioJsonBuf + prefixLen,
                                              RT_AUDIO_JSON_CAP - prefixLen - suffixLen);
        if (b64Len == 0) {
            Serial.println("[realtime] base64 encode failed");
            delay(1);
            return;
        }
        memcpy(rtAudioJsonBuf + prefixLen + b64Len, suffix, suffixLen + 1);
        const size_t jsonLen = prefixLen + b64Len + suffixLen;

        // Let the WiFi/TLS task run after I2S capture before the next write.
        for (int i = 0; i < 3; i++) {
            webSocket.loop();
            delay(1);
        }
        bool sent = sendTextChecked("audio_append", rtAudioJsonBuf, jsonLen);
        webSocket.loop();
        static uint32_t lastAudioLogMs = 0;
        if (!sent || (millis() - lastAudioLogMs >= 5000)) {
            int32_t peak = 0;
            for (int i = 0; i < rtRecLength; i++) {
                int32_t v = rtRecBuf[i];
                if (v < 0) v = -v;
                if (v > peak) peak = v;
            }
            Serial.printf("[realtime] audio append sent=%d capture_rate=%d wire_rate=%d cap_samples=%d wire_samples=%d pcm=%u json=%u peak=%d largestI=%u\n",
                          (int)sent,
                          rtRecSamplerate,
                          rtWireSamplerate,
                          rtRecLength,
                          wireSamples,
                          (unsigned)pcmBytes,
                          (unsigned)jsonLen,
                          (int)peak,
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            lastAudioLogMs = millis();
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
        state = getRealtimeStateSnapshot();
        if(state.speaking){
            //発話中もしくはテキスト生成中
            avatar.setSpeechText("");
            resetRealtimeRecordStartTime(); //長いテキストを発話中にタイムアウトしてしまうのを防ぐ
            delay(1);
        }
        else if(state.recordRequested && !state.sessionReady){
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
    bool queued = false;
    bool started = false;
    const uint32_t nowMs = millis();
    const portTickType nowTick = xTaskGetTickCount();
    portENTER_CRITICAL(&realtimeStateMux);
    RealtimeStateChange change = realtimeState.request(nowMs);
    RealtimeStateSnapshot state = realtimeState.snapshot();
    queued = !state.sessionReady && !state.speaking;
    started = change.started;
    if(started){
        startTime = nowTick;
    }
    portEXIT_CRITICAL(&realtimeStateMux);

    if(queued){
        Serial.println("[realtime] listen queued until session ready");
        avatar.setSpeechText("연결 중...");
        return;
    }
    if(started){
        Serial.println("Start realtime recording");
    }
}

void RealtimeLLMBase::stopRealtimeRecord()
{
    bool stopped;
    portENTER_CRITICAL(&realtimeStateMux);
    stopped = realtimeState.cancel().paused;
    startTime = 0;
    portEXIT_CRITICAL(&realtimeStateMux);
    if(stopped){
        Serial.println("Stop realtime recording");
    }
}

void RealtimeLLMBase::pauseRealtimeRecord(bool preserveRequest)
{
    bool stopped;
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&realtimeStateMux);
    stopped = realtimeState.pause(preserveRequest, nowMs).paused;
    startTime = 0;
    portEXIT_CRITICAL(&realtimeStateMux);
    if (stopped) Serial.println("Stop realtime recording");
}

bool RealtimeLLMBase::sendTextChecked(const char* label, const String& payload)
{
    return sendTextChecked(label, payload.c_str(), payload.length());
}

bool RealtimeLLMBase::sendTextChecked(const char* label, const char* payload, size_t length)
{
    bool sent = webSocket.sendTXT(payload, length);
    if (!sent) {
        Serial.printf("[WSc] send failed label=%s\n", label ? label : "unknown");
    }
    return sent;
}

void RealtimeLLMBase::setRealtimeSessionReady(bool ready)
{
    bool paused = false;
    bool resumed = false;
    const uint32_t nowMs = millis();
    const portTickType nowTick = xTaskGetTickCount();
    portENTER_CRITICAL(&realtimeStateMux);
    RealtimeStateChange change = realtimeState.setSessionReady(ready, nowMs);
    paused = change.paused;
    resumed = change.started;
    if (paused) startTime = 0;
    if (resumed) {
        startTime = nowTick;
    }
    portEXIT_CRITICAL(&realtimeStateMux);

    if (paused) Serial.println("[realtime] session lost — recording paused, resume queued");
    if (resumed) {
        Serial.println("[realtime] session ready — starting queued listen");
        Serial.println("Start realtime recording");
    }
}

void RealtimeLLMBase::setRealtimeSpeaking(bool value, bool resumeListening)
{
    bool resumed = false;
    const portTickType nowTick = xTaskGetTickCount();
    portENTER_CRITICAL(&realtimeStateMux);
    RealtimeStateChange change = realtimeState.setSpeaking(value, resumeListening);
    speaking = value;
    if (value) {
        startTime = 0;
    } else if (change.started) {
        startTime = nowTick;
        resumed = true;
    }
    portEXIT_CRITICAL(&realtimeStateMux);
    if (resumed) Serial.println("Start realtime recording");
}

bool RealtimeLLMBase::expireQueuedListen(uint32_t nowMs)
{
    bool expired = false;
    portENTER_CRITICAL(&realtimeStateMux);
    expired = realtimeState.expire(nowMs, RT_LISTEN_QUEUE_TIMEOUT_MS);
    if (expired) {
        startTime = 0;
    }
    portEXIT_CRITICAL(&realtimeStateMux);
    return expired;
}

RealtimeStateSnapshot RealtimeLLMBase::getRealtimeStateSnapshot()
{
    portENTER_CRITICAL(&realtimeStateMux);
    RealtimeStateSnapshot snapshot = realtimeState.snapshot();
    portEXIT_CRITICAL(&realtimeStateMux);
    return snapshot;
}

void RealtimeLLMBase::resetRealtimeRecordStartTime()
{
    const portTickType nowTick = xTaskGetTickCount();
    portENTER_CRITICAL(&realtimeStateMux);
    startTime = nowTick;
    portEXIT_CRITICAL(&realtimeStateMux);
}

portTickType RealtimeLLMBase::checkRealtimeRecordTimeout()
{
    const portTickType nowTick = xTaskGetTickCount();
    portTickType recordStart;
    portENTER_CRITICAL(&realtimeStateMux);
    recordStart = startTime;
    portEXIT_CRITICAL(&realtimeStateMux);
    portTickType elapsedTime = recordStart == 0
        ? 0
        : (nowTick - recordStart) * portTICK_RATE_MS;
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
    // M5Unified has two queue slots per virtual channel. Keep both slots fed and
    // only reuse a backing buffer after the older slot has been consumed. The
    // previous global isPlaying() wait inserted silence between every delta.
    if (!audioStreamActive) {
        audioStreamActive = true;
        audioChunkCount = 0;
        audioDecodedBytes = 0;
        audioQueueWaitMs = 0;
        audioQueueWaitMaxMs = 0;
        audioUnderrunCount = 0;
        audioEnqueueFailures = 0;
        nextBufIdx = 0;
        pendingFirstAudioLen = 0;
    }

    int base64Size = delta.length();
    uint8_t* buf = audioBuf[nextBufIdx];
    int len = base64_decode(delta.c_str(), base64Size, (char*)buf);
    if (len <= 0) {
        audioEnqueueFailures++;
        return;
    }

    audioChunkCount++;
    audioDecodedBytes += (uint32_t)len;

    // Hold the first fragment until the second arrives. Enqueuing both virtual
    // channel slots back-to-back prevents the first fragment from ending while
    // WebSocket parsing is still delivering the second one. A one-fragment
    // response is flushed by finishAudioStream().
    if (audioChunkCount == 1) {
        pendingFirstAudioLen = len;
        nextBufIdx = 1;
        return;
    }
    if (pendingFirstAudioLen > 0) {
        bool firstOk = M5.Speaker.playRaw((int16_t*)audioBuf[0],
                                          pendingFirstAudioLen / 2, 24000,
                                          false, 1, REALTIME_AUDIO_CHANNEL, false);
        bool secondOk = M5.Speaker.playRaw((int16_t*)audioBuf[1], len / 2, 24000,
                                           false, 1, REALTIME_AUDIO_CHANNEL, false);
        if (!firstOk) audioEnqueueFailures++;
        if (!secondOk) audioEnqueueFailures++;
        pendingFirstAudioLen = 0;
        nextBufIdx = secondOk ? 0 : (firstOk ? 1 : 0);
        return;
    }

    size_t queuedBefore = M5.Speaker.isPlaying(REALTIME_AUDIO_CHANNEL);
    if (queuedBefore == 0) audioUnderrunCount++;

    uint32_t waitStarted = millis();
    while (M5.Speaker.isPlaying(REALTIME_AUDIO_CHANNEL) >= 2) { vTaskDelay(1); }
    uint32_t waited = millis() - waitStarted;
    audioQueueWaitMs += waited;
    if (waited > audioQueueWaitMaxMs) audioQueueWaitMaxMs = waited;

    bool ok = M5.Speaker.playRaw((int16_t*)buf, len / 2, 24000, false, 1,
                                 REALTIME_AUDIO_CHANNEL, false);
    if (ok) {
        nextBufIdx ^= 1;
    } else {
        audioEnqueueFailures++;
    }
}

void RealtimeLLMBase::finishAudioStream(const char* reason)
{
    if (!audioStreamActive) return;
    if (pendingFirstAudioLen > 0) {
        bool ok = M5.Speaker.playRaw((int16_t*)audioBuf[0],
                                     pendingFirstAudioLen / 2, 24000,
                                     false, 1, REALTIME_AUDIO_CHANNEL, false);
        if (!ok) audioEnqueueFailures++;
        pendingFirstAudioLen = 0;
    }
    uint32_t drainStarted = millis();
    while (M5.Speaker.isPlaying(REALTIME_AUDIO_CHANNEL)) { vTaskDelay(1); }
    uint32_t drainMs = millis() - drainStarted;
    Serial.printf("[WSc] audio complete reason=%s chunks=%u bytes=%u queue_wait_ms=%u max_wait_ms=%u underruns=%u enqueue_fail=%u drain_ms=%u\n",
                  reason ? reason : "done",
                  (unsigned)audioChunkCount,
                  (unsigned)audioDecodedBytes,
                  (unsigned)audioQueueWaitMs,
                  (unsigned)audioQueueWaitMaxMs,
                  (unsigned)audioUnderrunCount,
                  (unsigned)audioEnqueueFailures,
                  (unsigned)drainMs);
    audioStreamActive = false;
}

void RealtimeLLMBase::abortAudioStream(const char* reason)
{
    if (!audioStreamActive) return;
    Serial.printf("[WSc] audio aborted reason=%s chunks=%u bytes=%u queued=%u underruns=%u enqueue_fail=%u\n",
                  reason ? reason : "disconnect",
                  (unsigned)audioChunkCount,
                  (unsigned)audioDecodedBytes,
                  (unsigned)M5.Speaker.isPlaying(REALTIME_AUDIO_CHANNEL),
                  (unsigned)audioUnderrunCount,
                  (unsigned)audioEnqueueFailures);
    audioStreamActive = false;
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
    if (webSocketLoopTask_h == NULL) return;
    if (eTaskGetState(webSocketLoopTask_h) != eSuspended) {
      Serial.println("webSocketLoopTask Suspend");
      vTaskSuspend(webSocketLoopTask_h);
    }
}

void RealtimeLLMBase::resumeWebSocketLoopTask(void)
{
    if (webSocketLoopTask_h == NULL) return;
    if (eTaskGetState(webSocketLoopTask_h) == eSuspended) {
      Serial.println("webSocketLoopTask Resume");
      vTaskResume(webSocketLoopTask_h);
    }
}

#endif  //REALTIME_API
