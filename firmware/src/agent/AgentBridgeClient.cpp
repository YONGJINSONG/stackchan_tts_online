#include "AgentBridgeClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "SpiRamJsonDocument.h"

namespace {
constexpr int32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t RESPONSE_TIMEOUT_MS = 60000;
constexpr uint32_t AGENT_TASK_STACK_SIZE = 12 * 1024;

bool isPlaceholder(const String& value) {
    return value.length() == 0 || value == "********";
}
}

struct AgentBridgeClient::AsyncRequest {
    AgentBridgeClient* client;
    AgentBridgeConfig config;
    String action;
    String text;
    uint32_t generation;
};

AgentBridgeClient::AgentBridgeClient()
    : _mutex(xSemaphoreCreateMutex()),
      _busy(false),
      _ready(false),
      _generation(0),
      _result("")
{
}

bool AgentBridgeClient::isSupportedAction(const String& action) {
    return action == "diary"
        || action == "search"
        || action == "shopping"
        || action == "memory";
}

String AgentBridgeClient::validate(const AgentBridgeConfig& cfg,
                                   const String& action,
                                   const String& text) const {
    if (!cfg.enabled) return "OpenClaw 연결이 꺼져 있어요.";
    if (WiFi.status() != WL_CONNECTED) return "와이파이에 연결되어 있지 않아요.";
    if (cfg.host.length() == 0) return "OpenClaw 브리지 PC 주소가 설정되지 않았어요.";
    if (cfg.host == "127.0.0.1" || cfg.host.equalsIgnoreCase("localhost")) {
        return "OpenClaw 브리지에는 PC의 내부 네트워크 주소를 설정해야 해요.";
    }
    if (cfg.host.startsWith("http://") || cfg.host.startsWith("https://")) {
        return "OpenClaw 브리지 주소에는 http 문자를 빼고 PC 주소만 넣어 주세요.";
    }
    if (cfg.profile != "kids" && cfg.profile != "adult") {
        return "OpenClaw 프로필 설정이 올바르지 않아요.";
    }
    if (cfg.deviceId.length() == 0) return "Stackchan 기기 이름이 설정되지 않았어요.";
    if (isPlaceholder(cfg.key)) return "OpenClaw 브리지 인증 키가 설정되지 않았어요.";
    if (!isSupportedAction(action)) return "OpenClaw에서 지원하지 않는 작업이에요.";
    if (text.length() == 0) return "OpenClaw에 전달할 요청이 비어 있어요.";
    return "";
}

String AgentBridgeClient::call(const AgentBridgeConfig& cfg,
                               const String& action,
                               const String& text) {
    String validationError = validate(cfg, action, text);
    if (validationError.length()) return validationError;

    String url = String("http://") + cfg.host + ":" + String(cfg.port) + "/v1/agent";
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(RESPONSE_TIMEOUT_MS);
    if (!http.begin(client, url)) {
        return "OpenClaw 브리지에 연결할 수 없어요.";
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Stackchan-Key", cfg.key);

    SpiRamJsonDocument requestDoc(text.length() + 512);
    requestDoc["profile"] = cfg.profile;
    requestDoc["device_id"] = cfg.deviceId;
    requestDoc["action"] = action;
    requestDoc["text"] = text;

    String body;
    body.reserve(text.length() + 256);
    serializeJson(requestDoc, body);

    int code = http.POST(body);
    if (code <= 0) {
        Serial.printf("[AgentBridge] transport error %d\n", code);
        http.end();
        return code == HTTPC_ERROR_READ_TIMEOUT
            ? "OpenClaw 응답 시간이 초과됐어요."
            : "OpenClaw 브리지에 연결할 수 없어요.";
    }
    if (code == 401 || code == 403) {
        Serial.printf("[AgentBridge] authentication failed HTTP %d\n", code);
        http.end();
        return "OpenClaw 브리지 인증 키를 확인해 주세요.";
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[AgentBridge] HTTP %d\n", code);
        http.end();
        return "OpenClaw에서 답을 받지 못했어요.";
    }

    SpiRamJsonDocument responseDoc(8 * 1024);
    DeserializationError jsonError = deserializeJson(responseDoc, http.getStream());
    String answer = responseDoc["text"] | "";
    http.end();

    if (jsonError) {
        Serial.printf("[AgentBridge] response JSON error: %s\n", jsonError.c_str());
        return "OpenClaw 응답을 읽지 못했어요.";
    }
    answer.trim();
    if (answer.length() == 0) return "OpenClaw 응답이 비어 있어요.";

    Serial.printf("[AgentBridge] completed action=%s chars=%u\n",
                  action.c_str(), static_cast<unsigned>(answer.length()));
    return answer;
}

bool AgentBridgeClient::start(const AgentBridgeConfig& cfg,
                              const String& action,
                              const String& text) {
    if (_mutex == nullptr) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_busy) {
        xSemaphoreGive(_mutex);
        return false;
    }
    xSemaphoreGive(_mutex);

    String validationError = validate(cfg, action, text);
    if (validationError.length()) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_busy) {
            xSemaphoreGive(_mutex);
            return false;
        }
        _result = validationError;
        _ready = true;
        xSemaphoreGive(_mutex);
        return true;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_busy) {
        xSemaphoreGive(_mutex);
        return false;
    }
    _busy = true;
    _ready = false;
    _result = "";
    const uint32_t generation = ++_generation;
    xSemaphoreGive(_mutex);

    AsyncRequest* request = new AsyncRequest{this, cfg, action, text, generation};
    if (request == nullptr) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _busy = false;
        _result = "OpenClaw 요청을 시작할 메모리가 부족해요.";
        _ready = true;
        xSemaphoreGive(_mutex);
        return true;
    }

    BaseType_t created = xTaskCreate(taskEntry, "agent_bridge",
                                     AGENT_TASK_STACK_SIZE, request, 1, nullptr);
    if (created != pdPASS) {
        delete request;
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _busy = false;
        _result = "OpenClaw 요청 작업을 시작하지 못했어요.";
        _ready = true;
        xSemaphoreGive(_mutex);
    }
    return true;
}

void AgentBridgeClient::taskEntry(void* arg) {
    AsyncRequest* request = static_cast<AsyncRequest*>(arg);
    AgentBridgeClient* client = request->client;
    String result = client->call(request->config, request->action, request->text);

    xSemaphoreTake(client->_mutex, portMAX_DELAY);
    if (request->generation == client->_generation) {
        client->_result = result;
        client->_ready = true;
    }
    client->_busy = false;
    xSemaphoreGive(client->_mutex);

    delete request;
    vTaskDelete(nullptr);
}

bool AgentBridgeClient::takeResult(String& result) {
    if (_mutex == nullptr) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_ready) {
        xSemaphoreGive(_mutex);
        return false;
    }
    result = _result;
    _result = "";
    _ready = false;
    xSemaphoreGive(_mutex);
    return true;
}

void AgentBridgeClient::abandon() {
    if (_mutex == nullptr) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    ++_generation;
    _ready = false;
    _result = "";
    xSemaphoreGive(_mutex);
}

bool AgentBridgeClient::isBusy() {
    if (_mutex == nullptr) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool busy = _busy;
    xSemaphoreGive(_mutex);
    return busy;
}
