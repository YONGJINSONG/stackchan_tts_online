#if defined(REALTIME_API)

#ifndef _REALTIME_LLM_BASE_H
#define _REALTIME_LLM_BASE_H

#include <Arduino.h>
#include <M5Unified.h>
#include "StackchanExConfig.h"
#include "SpiRamJsonDocument.h"
#include "ChatHistory.h"
#include "LLMBase.h"
#include "RealtimeListenState.h"
#include <WebSocketsClient.h>

//#define REALTIME_API_RECORD_TEST

#define GEMINI_PROMPT_MAX_SIZE   (1024*50)

#define RT_REC_BUFFER_SAMPLES       (3000)
#define RT_REC_DEFAULT_LENGTH       (2000)      // 16 kHz PCM: 125 ms per chunk
#define RT_REC_DEFAULT_SAMPLE_RATE  (16000)
#define RT_LISTEN_QUEUE_TIMEOUT_MS  (60 * 1000)

#ifdef REALTIME_API_RECORD_TEST
#define REALTIME_RECORD_TIMEOUT     (4 * 1000)      //ms  ※録音テスト再生用バッファのサイズに合わせる
#else
#define REALTIME_RECORD_TIMEOUT     (30 * 1000)      //ms
#endif

extern String InitBuffer;
extern const String json_ChatString;

class RealtimeLLMBase: public LLMBase{
//private:
public:   //本当はprivateにしたいところだがコールバック関数にthisポインタを渡して使うためにpublicとした
    WebSocketsClient webSocket;
    SpiRamJsonDocument msgDoc;

    // for record
    //
    //int16_t* rtRecBuf;
    int rtRecSamplerate;
    int rtWireSamplerate;
    int rtRecLength;
    bool response_done;
    portTickType startTime;

private:
    portMUX_TYPE realtimeStateMux = portMUX_INITIALIZER_UNLOCKED;
    RealtimeListenState realtimeState;

public:
#ifdef REALTIME_API_RECORD_TEST
    int16_t* recTestBuf;
    int recTestLenMax;
    int recTestLenCnt;
#endif

    // for play
    //
    uint8_t* audioBuf[2];    // Base64をデコードして得た音声データを格納するバッファ。再生直後に更新すると音が切れたのでダブルバッファとした
    int nextBufIdx;          // 次回データを格納するダブルバッファの面（0 or 1）
    static constexpr uint8_t REALTIME_AUDIO_CHANNEL = 0;
    bool audioStreamActive;
    int pendingFirstAudioLen;
    uint32_t audioChunkCount;
    uint32_t audioDecodedBytes;
    uint32_t audioQueueWaitMs;
    uint32_t audioQueueWaitMaxMs;
    uint32_t audioUnderrunCount;
    uint32_t audioEnqueueFailures;

public:
    RealtimeLLMBase(llm_param_t param,
                    int captureSampleRate = RT_REC_DEFAULT_SAMPLE_RATE,
                    int captureSamples = RT_REC_DEFAULT_LENGTH,
                    int wireSampleRate = RT_REC_DEFAULT_SAMPLE_RATE);

    virtual void chat(String text, const char *base64_buf = NULL) {};   //dummy
    virtual String& buildInputAudioJson(String& jsonBuf, String& base64) = 0;
    virtual void audioAppendEnvelope(const char*& prefix, const char*& suffix) = 0;
    virtual bool sendTextChecked(const char* label, const String& payload);
    virtual bool sendTextChecked(const char* label, const char* payload, size_t length);

    // Inject a proactive user-role message and trigger a response. Used for
    // scheduled greetings (midnight bedtime, weekday morning briefings).
    // Default no-op; concrete realtime LLMs override. MUST be cheap and
    // thread-safe: it is called from the loop/HTTP/scheduler task, NOT the
    // WebSocket task, so it may only enqueue — never touch webSocket directly.
    virtual void pushUserText(const String& text) {}

    // Called from the WebSocket task each iteration (after webSocket.loop()), so
    // it is the only safe place outside the event callback to touch webSocket.
    // Concrete realtime LLMs use it to drain queued proactive text and to run the
    // response-timeout watchdog. Default no-op.
    virtual void onWebSocketTick() {}

    void invokeWebSocketLoopTask(void);
    void suspendWebSocketLoopTask(void);
    void resumeWebSocketLoopTask(void);
    void webSocketProcess();
    int getAudioLevel();
    void startRealtimeRecord();
    void stopRealtimeRecord();
    void pauseRealtimeRecord(bool preserveRequest = true);
    void setRealtimeSessionReady(bool ready);
    void setRealtimeSpeaking(bool value, bool resumeListening = true);
    bool expireQueuedListen(uint32_t nowMs);
    RealtimeStateSnapshot getRealtimeStateSnapshot();
    void resetRealtimeRecordStartTime();
    portTickType checkRealtimeRecordTimeout();
    bool isRealtimeRecording() {return getRealtimeStateSnapshot().recording;};
    bool isRealtimeRecordRequested() {return getRealtimeStateSnapshot().recordRequested;};
    bool isRealtimeSessionReady() {return getRealtimeStateSnapshot().sessionReady;};
    bool isSpeaking() override {return getRealtimeStateSnapshot().speaking;};

    int base64_decode(const char* input, int size, char* output);
    void hexdump(const void *mem, uint32_t len, uint8_t cols = 16);
    void streamAudioDelta(String& delta);
    void finishAudioStream(const char* reason);
    void abortAudioStream(const char* reason);

    // for TTS
    //
    String outputText;

};


#endif  //_REALTIME_LLM_BASE_H

#endif  //REALTIME_API
