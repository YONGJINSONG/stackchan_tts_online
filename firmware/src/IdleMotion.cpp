#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Avatar.h>
#include "IdleMotion.h"
#include "Gesture.h"
#include "NightMode.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;

extern Avatar avatar;
extern volatile uint32_t gesture_suppress_until;

#define IDLE_SPIFFS_PATH "/idlemotion.json"

// Expression order matches expressions_table[] in main.cpp:
// 0 Neutral, 1 Happy, 2 Sleepy, 3 Doubt, 4 Sad, 5 Angry.
static const Expression IDLE_EXPR[] = {
    Expression::Neutral, Expression::Happy, Expression::Sleepy,
    Expression::Doubt, Expression::Sad, Expression::Angry,
};
#define IDLE_EXPR_COUNT 6

struct IdleConfig {
    bool enabled = true;

    // Legacy public aliases.  They mirror the expression interval so older
    // /idle_set clients retain a useful meaning.
    int minIntervalSec = 90;
    int maxIntervalSec = 150;

    int expressionMinSec = 90;
    int expressionMaxSec = 150;
    int gazeMinSec = 75;
    int gazeMaxSec = 120;
    int headMinSec = 150;
    int headMaxSec = 210;

    int quietAfterSec = 20;
    int energy = 5;  // 5 = configured interval, 1 slower, 10 faster.
    bool gestureEnabled = true;
    bool gazeWander = true;
    bool blinkEnabled = true;
    int blinkMinSec = 5;
    int blinkMaxSec = 10;
    // Weights for [Neutral, Happy, Sleepy, Doubt, Sad, Angry].
    int exprWeights[IDLE_EXPR_COUNT] = {12, 3, 0, 4, 0, 0};
};

static IdleConfig g_cfg;
static portMUX_TYPE g_cfgMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t g_holdUntil = 0;

void idle_motion_hold(uint32_t ms) {
    const uint32_t until = millis() + ms;
    if (until > g_holdUntil) g_holdUntil = until;
}

static void clampConfig(IdleConfig& c);
static void buildJson(const IdleConfig& c, String& out);

static bool weightsEqual(const IdleConfig& c, const int (&weights)[IDLE_EXPR_COUNT]) {
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) {
        if (c.exprWeights[i] != weights[i]) return false;
    }
    return true;
}

static bool isLegacyDefault(const IdleConfig& c) {
    static const int kWeights[IDLE_EXPR_COUNT] = {8, 4, 2, 3, 0, 0};
    return c.minIntervalSec == 25 && c.maxIntervalSec == 50 &&
           c.quietAfterSec == 8 && c.energy == 5 &&
           c.blinkMinSec == 3 && c.blinkMaxSec == 9 &&
           weightsEqual(c, kWeights);
}

static bool isFriendlyDefault(const IdleConfig& c) {
    static const int kWeights[IDLE_EXPR_COUNT] = {12, 3, 0, 4, 0, 0};
    return c.minIntervalSec == 40 && c.maxIntervalSec == 70 &&
           c.quietAfterSec == 15 && c.energy == 5 &&
           c.blinkMinSec == 4 && c.blinkMaxSec == 8 &&
           weightsEqual(c, kWeights);
}

static void applyCalmDefaults(IdleConfig& c) {
    c.expressionMinSec = 90;
    c.expressionMaxSec = 150;
    c.gazeMinSec = 75;
    c.gazeMaxSec = 120;
    c.headMinSec = 150;
    c.headMaxSec = 210;
    c.minIntervalSec = c.expressionMinSec;
    c.maxIntervalSec = c.expressionMaxSec;
    c.quietAfterSec = 20;
    c.energy = 5;
    c.blinkMinSec = 5;
    c.blinkMaxSec = 10;
    const int weights[IDLE_EXPR_COUNT] = {12, 3, 0, 4, 0, 0};
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) c.exprWeights[i] = weights[i];
}

static void clampInterval(int& minSec, int& maxSec) {
    if (minSec < 1) minSec = 1;
    if (maxSec < minSec) maxSec = minSec;
    if (maxSec > 600) maxSec = 600;
}

static void clampConfig(IdleConfig& c) {
    clampInterval(c.minIntervalSec, c.maxIntervalSec);
    clampInterval(c.expressionMinSec, c.expressionMaxSec);
    clampInterval(c.gazeMinSec, c.gazeMaxSec);
    clampInterval(c.headMinSec, c.headMaxSec);
    if (c.quietAfterSec < 0) c.quietAfterSec = 0;
    if (c.energy < 1) c.energy = 1;
    if (c.energy > 10) c.energy = 10;
    clampInterval(c.blinkMinSec, c.blinkMaxSec);
    int totalWeight = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) {
        if (c.exprWeights[i] < 0) c.exprWeights[i] = 0;
        totalWeight += c.exprWeights[i];
    }
    if (totalWeight == 0) c.exprWeights[0] = 1;
}

static bool writeConfig(const IdleConfig& c) {
    String out;
    buildJson(c, out);
    File f = SPIFFS.open(IDLE_SPIFFS_PATH, "w");
    if (!f) {
        Serial.println("[idle] SPIFFS open(w) failed");
        return false;
    }
    f.print(out);
    f.close();
    return true;
}

static void load_from_spiffs() {
    if (!SPIFFS.exists(IDLE_SPIFFS_PATH)) {
        Serial.println("[idle] no idlemotion.json - using calm defaults");
        return;
    }
    File f = SPIFFS.open(IDLE_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, body)) {
        Serial.println("[idle] idlemotion.json parse error - using calm defaults");
        return;
    }

    IdleConfig c;
    c.enabled = doc["enabled"] | c.enabled;
    c.minIntervalSec = doc["minIntervalSec"] | c.minIntervalSec;
    c.maxIntervalSec = doc["maxIntervalSec"] | c.maxIntervalSec;
    c.quietAfterSec = doc["quietAfterSec"] | c.quietAfterSec;
    c.energy = doc["energy"] | c.energy;
    c.gestureEnabled = doc["gestureEnabled"] | c.gestureEnabled;
    c.gazeWander = doc["gazeWander"] | c.gazeWander;
    c.blinkEnabled = doc["blinkEnabled"] | c.blinkEnabled;
    c.blinkMinSec = doc["blinkMinSec"] | c.blinkMinSec;
    c.blinkMaxSec = doc["blinkMaxSec"] | c.blinkMaxSec;
    JsonArray weights = doc["exprWeights"].as<JsonArray>();
    if (!weights.isNull()) {
        for (int i = 0; i < IDLE_EXPR_COUNT && i < (int)weights.size(); ++i) {
            c.exprWeights[i] = weights[i];
        }
    }

    const bool hasIndependentIntervals =
        doc.containsKey("expressionMinSec") || doc.containsKey("expressionMaxSec") ||
        doc.containsKey("gazeMinSec") || doc.containsKey("gazeMaxSec") ||
        doc.containsKey("headMinSec") || doc.containsKey("headMaxSec");
    if (hasIndependentIntervals) {
        c.expressionMinSec = doc["expressionMinSec"] | c.expressionMinSec;
        c.expressionMaxSec = doc["expressionMaxSec"] | c.expressionMaxSec;
        c.gazeMinSec = doc["gazeMinSec"] | c.gazeMinSec;
        c.gazeMaxSec = doc["gazeMaxSec"] | c.gazeMaxSec;
        c.headMinSec = doc["headMinSec"] | c.headMinSec;
        c.headMaxSec = doc["headMaxSec"] | c.headMaxSec;
    }

    bool migrated = false;
    if (!hasIndependentIntervals) {
        if ((c.minIntervalSec == 6 && c.maxIntervalSec == 14) ||
            isLegacyDefault(c) || isFriendlyDefault(c)) {
            applyCalmDefaults(c);
            migrated = true;
        } else {
            // A custom legacy interval used to govern all idle behaviour.
            // Preserve that intent when splitting the schedules.
            c.expressionMinSec = c.minIntervalSec;
            c.expressionMaxSec = c.maxIntervalSec;
            c.gazeMinSec = c.minIntervalSec;
            c.gazeMaxSec = c.maxIntervalSec;
            c.headMinSec = c.minIntervalSec;
            c.headMaxSec = c.maxIntervalSec;
            migrated = true;
        }
    }
    clampConfig(c);
    c.minIntervalSec = c.expressionMinSec;
    c.maxIntervalSec = c.expressionMaxSec;

    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);

    if (migrated) {
        if (writeConfig(c)) Serial.println("[idle] migrated saved config to split schedules");
    } else {
        Serial.println("[idle] loaded config from SPIFFS");
    }
}

static void buildJson(const IdleConfig& c, String& out) {
    DynamicJsonDocument doc(2048);
    doc["enabled"] = c.enabled;
    // Preserve the old API as aliases for the expression schedule.
    doc["minIntervalSec"] = c.expressionMinSec;
    doc["maxIntervalSec"] = c.expressionMaxSec;
    doc["expressionMinSec"] = c.expressionMinSec;
    doc["expressionMaxSec"] = c.expressionMaxSec;
    doc["gazeMinSec"] = c.gazeMinSec;
    doc["gazeMaxSec"] = c.gazeMaxSec;
    doc["headMinSec"] = c.headMinSec;
    doc["headMaxSec"] = c.headMaxSec;
    doc["quietAfterSec"] = c.quietAfterSec;
    doc["energy"] = c.energy;
    doc["gestureEnabled"] = c.gestureEnabled;
    doc["gazeWander"] = c.gazeWander;
    doc["blinkEnabled"] = c.blinkEnabled;
    doc["blinkMinSec"] = c.blinkMinSec;
    doc["blinkMaxSec"] = c.blinkMaxSec;
    JsonArray weights = doc.createNestedArray("exprWeights");
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) weights.add(c.exprWeights[i]);
    serializeJson(doc, out);
}

String idle_motion_get_json() {
    IdleConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    String out;
    buildJson(c, out);
    return out;
}

bool idle_motion_set_json(const String& json) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;

    IdleConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);

    if (doc.containsKey("enabled")) c.enabled = doc["enabled"];
    const bool hasLegacyInterval = doc.containsKey("minIntervalSec") || doc.containsKey("maxIntervalSec");
    if (doc.containsKey("minIntervalSec")) c.minIntervalSec = doc["minIntervalSec"];
    if (doc.containsKey("maxIntervalSec")) c.maxIntervalSec = doc["maxIntervalSec"];

    const bool hasIndependentIntervals =
        doc.containsKey("expressionMinSec") || doc.containsKey("expressionMaxSec") ||
        doc.containsKey("gazeMinSec") || doc.containsKey("gazeMaxSec") ||
        doc.containsKey("headMinSec") || doc.containsKey("headMaxSec");
    if (hasLegacyInterval && !hasIndependentIntervals) {
        c.expressionMinSec = c.minIntervalSec;
        c.expressionMaxSec = c.maxIntervalSec;
        c.gazeMinSec = c.minIntervalSec;
        c.gazeMaxSec = c.maxIntervalSec;
        c.headMinSec = c.minIntervalSec;
        c.headMaxSec = c.maxIntervalSec;
    }
    if (doc.containsKey("expressionMinSec")) c.expressionMinSec = doc["expressionMinSec"];
    if (doc.containsKey("expressionMaxSec")) c.expressionMaxSec = doc["expressionMaxSec"];
    if (doc.containsKey("gazeMinSec")) c.gazeMinSec = doc["gazeMinSec"];
    if (doc.containsKey("gazeMaxSec")) c.gazeMaxSec = doc["gazeMaxSec"];
    if (doc.containsKey("headMinSec")) c.headMinSec = doc["headMinSec"];
    if (doc.containsKey("headMaxSec")) c.headMaxSec = doc["headMaxSec"];
    if (doc.containsKey("quietAfterSec")) c.quietAfterSec = doc["quietAfterSec"];
    if (doc.containsKey("energy")) c.energy = doc["energy"];
    if (doc.containsKey("gestureEnabled")) c.gestureEnabled = doc["gestureEnabled"];
    if (doc.containsKey("gazeWander")) c.gazeWander = doc["gazeWander"];
    if (doc.containsKey("blinkEnabled")) c.blinkEnabled = doc["blinkEnabled"];
    if (doc.containsKey("blinkMinSec")) c.blinkMinSec = doc["blinkMinSec"];
    if (doc.containsKey("blinkMaxSec")) c.blinkMaxSec = doc["blinkMaxSec"];
    JsonArray weights = doc["exprWeights"].as<JsonArray>();
    if (!weights.isNull()) {
        for (int i = 0; i < IDLE_EXPR_COUNT && i < (int)weights.size(); ++i) {
            c.exprWeights[i] = weights[i];
        }
    }

    clampConfig(c);
    c.minIntervalSec = c.expressionMinSec;
    c.maxIntervalSec = c.expressionMaxSec;
    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);

    if (!writeConfig(c)) return false;
    Serial.println("[idle] saved config to SPIFFS");
    return true;
}

static int pickExpr(const IdleConfig& c) {
    int totalWeight = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) totalWeight += c.exprWeights[i];
    if (totalWeight <= 0) return 0;
    int value = (int)random(totalWeight);
    for (int i = 0; i < IDLE_EXPR_COUNT; ++i) {
        value -= c.exprWeights[i];
        if (value < 0) return i;
    }
    return 0;
}

static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) {
        return ((RealtimeLLMBase*)(robot->llm))->getAudioLevel() > 200;
    }
#endif
    return false;
}

static float energyScale(int energy) {
    // Keep the configured intervals literal at the default energy level.
    return energy <= 5 ? 1.0f + (5 - energy) * 0.1f
                       : 1.0f - (energy - 5) * 0.1f;
}

static uint32_t intervalMs(int minSec, int maxSec, float scale = 1.0f) {
    const int seconds = minSec + (maxSec > minSec ? random(maxSec - minSec + 1) : 0);
    uint32_t value = (uint32_t)(seconds * 1000.0f * scale);
    return value < 1000 ? 1000 : value;
}

static void resetSchedules(uint32_t now, const IdleConfig& c, float scale,
                           uint32_t& nextExpressionAt, uint32_t& nextGazeAt,
                           uint32_t& nextHeadAt, uint32_t& nextBlinkAt) {
    nextExpressionAt = now + intervalMs(c.expressionMinSec, c.expressionMaxSec, scale);
    nextGazeAt = now + intervalMs(c.gazeMinSec, c.gazeMaxSec, scale);
    nextHeadAt = now + intervalMs(c.headMinSec, c.headMaxSec, scale);
    nextBlinkAt = now + intervalMs(c.blinkMinSec, c.blinkMaxSec);
}

static void idle_task(void* arg) {
    IdleConfig initial;
    portENTER_CRITICAL(&g_cfgMux);
    initial = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    const uint32_t bootNow = millis();
    uint32_t nextExpressionAt = bootNow + 60000;
    uint32_t nextGazeAt = bootNow + 60000;
    uint32_t nextHeadAt = bootNow + 90000;
    uint32_t nextBlinkAt = bootNow + intervalMs(initial.blinkMinSec, initial.blinkMaxSec);
    uint32_t lastSpeakMs = 0;
    bool sleeping = false;
    bool wasProtected = false;

    for (;;) {
        delay(250);

        IdleConfig c;
        portENTER_CRITICAL(&g_cfgMux);
        c = g_cfg;
        portEXIT_CRITICAL(&g_cfgMux);
        if (robot == nullptr) continue;

        const uint32_t now = millis();
        const float scale = energyScale(c.energy);
        const bool isSleeping = night_mode_is_sleeping();
        if (isSleeping) {
            if (!sleeping) {
                avatar.setExpression(Expression::Sleepy);
                avatar.setGaze(0.0f, 0.0f);
                avatar.setEyeOpenRatio(1.0f);
                Serial.println("[idle] sleep: sleepy eyes fixed; idle motion paused");
            }
            sleeping = true;
            wasProtected = false;
            continue;
        }
        if (sleeping) {
            sleeping = false;
            avatar.setExpression(Expression::Neutral);
            avatar.setGaze(0.0f, 0.0f);
            avatar.setEyeOpenRatio(1.0f);
            resetSchedules(now, c, scale, nextExpressionAt, nextGazeAt, nextHeadAt, nextBlinkAt);
            Serial.println("[idle] wake: calm idle schedules restarted");
            continue;
        }

        if (!c.enabled) {
            lastSpeakMs = now;
            wasProtected = true;
            continue;
        }

        const bool speaking = is_speaking();
        if (speaking) lastSpeakMs = now;
        const bool protectedState = speaking || gesture_motion_held() ||
                                    now < g_holdUntil || now < gesture_suppress_until ||
                                    now - lastSpeakMs < (uint32_t)c.quietAfterSec * 1000UL;
        if (protectedState) {
            if (!wasProtected) {
                resetSchedules(now, c, scale, nextExpressionAt, nextGazeAt, nextHeadAt, nextBlinkAt);
            }
            wasProtected = true;
            continue;
        }
        if (wasProtected) {
            resetSchedules(now, c, scale, nextExpressionAt, nextGazeAt, nextHeadAt, nextBlinkAt);
            wasProtected = false;
        }

        if (c.blinkEnabled && now >= nextBlinkAt) {
            avatar.setEyeOpenRatio(0.0f);
            delay(120);
            avatar.setEyeOpenRatio(1.0f);
            nextBlinkAt = now + intervalMs(c.blinkMinSec, c.blinkMaxSec);
        }

        if (c.gazeWander && now >= nextGazeAt) {
            // Gentle horizontal attention shift; avoid vertical jitter.
            const float gazeX = (random(51) - 25) / 100.0f;  // -0.25 .. 0.25
            avatar.setGaze(0.0f, gazeX);
            nextGazeAt = now + intervalMs(c.gazeMinSec, c.gazeMaxSec, scale);
        }

        if (now >= nextExpressionAt) {
            avatar.setExpression(IDLE_EXPR[pickExpr(c)]);
            nextExpressionAt = now + intervalMs(c.expressionMinSec, c.expressionMaxSec, scale);
        }

        if (c.gestureEnabled && now >= nextHeadAt) {
            gesture_idle_look();
            nextHeadAt = now + intervalMs(c.headMinSec, c.headMaxSec, scale);
        }
    }
}

void idle_motion_init() {
    load_from_spiffs();
    xTaskCreatePinnedToCore(idle_task, "idle_motion", 4096, NULL, 1, NULL, APP_CPU_NUM);
    Serial.println("[idle] task started (split calm schedules)");
}
