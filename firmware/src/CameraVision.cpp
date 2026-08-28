#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "CameraVision.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif
#if defined(ENABLE_CAMERA)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "driver/Camera.h"
#endif

extern bool isOffline;

static volatile bool s_hardware_busy = false;
static volatile bool s_vision_busy = false;
static SemaphoreHandle_t s_sensor_bus_mutex = nullptr;
bool camera_is_busy() { return s_hardware_busy; }
void camera_set_hardware_busy(bool busy) { s_hardware_busy = busy; }
void camera_sensor_bus_lock() {
    if (s_sensor_bus_mutex == nullptr) {
        s_sensor_bus_mutex = xSemaphoreCreateMutex();
    }
    if (s_sensor_bus_mutex != nullptr) xSemaphoreTake(s_sensor_bus_mutex, portMAX_DELAY);
}
bool camera_sensor_bus_try_lock(uint32_t timeout_ms) {
    if (s_sensor_bus_mutex == nullptr) {
        s_sensor_bus_mutex = xSemaphoreCreateMutex();
    }
    if (s_sensor_bus_mutex == nullptr) return false;
    return xSemaphoreTake(s_sensor_bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
void camera_sensor_bus_unlock() {
    if (s_sensor_bus_mutex != nullptr) xSemaphoreGive(s_sensor_bus_mutex);
}
void camera_vision_init() {
    if (s_sensor_bus_mutex == nullptr) s_sensor_bus_mutex = xSemaphoreCreateMutex();
}

String camera_vision_describe(const String& hint) {
#if defined(ENABLE_CAMERA)
    if (isOffline || robot == nullptr || robot->llm == nullptr) return String();
    if (s_vision_busy) return String();
    s_vision_busy = true;

    String b64;
    bool ok = camera_capture_base64(b64);
    if (!ok || b64.length() == 0) { s_vision_busy = false; Serial.println("[vision] capture failed"); return String(); }

    String p = hint.length() ? hint
                             : String("이 이미지에 보이는 것을 한국어로 한 문장으로 아주 간단히 설명해줘.");
    p.replace("\\", "\\\\"); p.replace("\"", "\\\""); p.replace("\n", " ");

    String body;
    body.reserve(b64.length() + 512);
    body  = "{\"model\":\"gpt-4o-mini\",\"max_tokens\":120,\"messages\":[{\"role\":\"user\",\"content\":[";
    body += "{\"type\":\"text\",\"text\":\"";
    body += p;
    body += "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    body += b64;
    body += "\"}}]}]}";
    b64 = String();

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(15000);
    if (!https.begin(client, "https://api.openai.com/v1/chat/completions")) {
        s_vision_busy = false; Serial.println("[vision] https begin failed"); return String();
    }
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", String("Bearer ") + robot->llm->param.api_key);
    int code = https.POST(body);
    String resp = (code > 0) ? https.getString() : String("");
    https.end();
    body = String();
    s_vision_busy = false;

    if (code != 200) {
        Serial.printf("[vision] HTTP %d: %s\n", code, resp.substring(0, 160).c_str());
        return String();
    }

    SpiRamJsonDocument doc(resp.length() + 1024);
    if (deserializeJson(doc, resp)) { Serial.println("[vision] response parse error"); return String(); }
    const char* desc = doc["choices"][0]["message"]["content"] | "";
    if (!desc || !*desc) { Serial.println("[vision] empty content"); return String(); }
    Serial.printf("[vision] %s\n", desc);
    return String(desc);
#else
    (void)hint;
    return String();
#endif
}

bool camera_vision_look(const String& hint) {
    String desc = camera_vision_describe(hint);
    if (desc.length() == 0) return false;

#if defined(REALTIME_API)
    String inject = String("[방금 카메라로 본 것: ") + desc +
                    "] 이걸 가족에게 자연스럽게 한두 문장으로 말해줘. 너무 길지 않게.";
    ((RealtimeLLMBase*)robot->llm)->pushUserText(inject);
    return true;
#else
    if (robot) robot->speech(desc);
    return true;
#endif
}
