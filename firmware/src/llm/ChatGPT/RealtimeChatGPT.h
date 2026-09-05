#if defined(REALTIME_API)

#ifndef _REALTIME_CHAT_GPT_H
#define _REALTIME_CHAT_GPT_H

#include <Arduino.h>
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "StackchanExConfig.h"
#include "SpiRamJsonDocument.h"
#include "../ChatHistory.h"
#include "../RealtimeLLMBase.h"
#include "ChatGPT.h"
#include <WebSocketsClient.h>


class RealtimeChatGPT: public RealtimeLLMBase{
public:   //本当はprivateにしたいところだがコールバック関数にthisポインタを渡して使うためにpublicとした
    MCPClient* mcpClient[LLM_N_MCP_SERVERS_MAX];
    FunctionCall* fnCall;

    String role;
    String userInfo;
    String systemRole;

    // Proactive-speech queue. pushUserText() (loop/HTTP/scheduler task) only
    // enqueues; onWebSocketTick() (WS task) drains and sends, keeping all
    // webSocket access on one task. Single slot: bursts coalesce (newest dropped).
    SemaphoreHandle_t proactiveMux = NULL;
    String pendingProactiveText;
    volatile bool hasPendingProactive = false;

    // Any task may queue a reconnect, but only onWebSocketTick() touches the socket.
    SemaphoreHandle_t reconnectMux = NULL;
    bool reconnectRequest = false;
    String reconnectReason;
    uint32_t sessionUpdateSentAt = 0;
    uint8_t sessionFailureCount = 0;
    bool suppressDisconnectFailure = false;

    volatile bool hasDeferredFunctionCall = false;
    String deferredFunctionCallId;

public:
    RealtimeChatGPT(llm_param_t param);

    virtual void chat(String text, const char *base64_buf = NULL) {};   //dummy
    virtual String& buildInputAudioJson(String& jsonBuf, String& base64);
    void audioAppendEnvelope(const char*& prefix, const char*& suffix) override;
    bool sendTextChecked(const char* label, const String& payload) override;
    bool sendTextChecked(const char* label, const char* payload, size_t length) override;
    virtual void load_role();
    void pushUserText(const String& text) override;
    void onWebSocketTick() override;
    void requestReconnect() override;
    void queueReconnect(const char* reason);
    void noteSessionFailure(const char* reason, const String& detail = String(),
                            bool requestReconnectNow = true);
};


#endif  //_REALTIME_CHAT_GPT_H

#endif  //REALTIME_API
