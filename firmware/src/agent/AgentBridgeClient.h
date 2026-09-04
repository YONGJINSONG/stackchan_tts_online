#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct AgentBridgeConfig {
    bool enabled = false;
    String host = "";
    uint16_t port = 8765;
    bool tls = false;
    String profile = "kids";
    String deviceId = "roni";
    String key = "";
};

class AgentBridgeClient {
public:
    AgentBridgeClient();

    String call(const AgentBridgeConfig& cfg,
                const String& action,
                const String& text);

    bool start(const AgentBridgeConfig& cfg,
               const String& action,
               const String& text);
    bool takeResult(String& result);
    void abandon();
    bool isBusy();

private:
    struct AsyncRequest;

    static void taskEntry(void* arg);
    static bool isSupportedAction(const String& action);
    String validate(const AgentBridgeConfig& cfg,
                    const String& action,
                    const String& text) const;

    SemaphoreHandle_t _mutex;
    bool _busy;
    bool _ready;
    uint32_t _generation;
    String _result;
};
