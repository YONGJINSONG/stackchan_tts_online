#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "TouchReaction.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "PetReaction.h"
#include "CameraVision.h"
#include "NightMode.h"

using namespace m5avatar;
extern Avatar avatar;

#define TOUCH_SPIFFS_PATH  "/touch.json"

// Config is only touched from the loop task — no mutex needed.
static bool g_lookEnabled    = true;
static bool g_strokeEnabled  = true;
static int  g_strokeThreshold = 60;   // accumulated drag pixels to count as a stroke

static void load_from_spiffs() {
    if (!SPIFFS.exists(TOUCH_SPIFFS_PATH)) {
        Serial.println("[touch] no touch.json — using defaults");
        return;
    }
    File f = SPIFFS.open(TOUCH_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body)) { Serial.println("[touch] parse error — defaults"); return; }
    g_lookEnabled     = doc["lookEnabled"]     | g_lookEnabled;
    g_strokeEnabled   = doc["strokeEnabled"]   | g_strokeEnabled;
    g_strokeThreshold = doc["strokeThreshold"] | g_strokeThreshold;
    if (g_strokeThreshold < 20) g_strokeThreshold = 20;
    Serial.println("[touch] loaded config from SPIFFS");
}

String touch_reaction_get_json() {
    DynamicJsonDocument doc(512);
    doc["lookEnabled"]     = g_lookEnabled;
    doc["strokeEnabled"]   = g_strokeEnabled;
    doc["strokeThreshold"] = g_strokeThreshold;
    String out; serializeJson(doc, out); return out;
}

bool touch_reaction_set_json(const String& json) {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("lookEnabled"))     g_lookEnabled     = doc["lookEnabled"];
    if (doc.containsKey("strokeEnabled"))   g_strokeEnabled   = doc["strokeEnabled"];
    if (doc.containsKey("strokeThreshold")) g_strokeThreshold = doc["strokeThreshold"];
    if (g_strokeThreshold < 20) g_strokeThreshold = 20;
    File f = SPIFFS.open(TOUCH_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[touch] SPIFFS open(w) failed"); return false; }
    f.print(touch_reaction_get_json());
    f.close();
    Serial.println("[touch] saved config to SPIFFS");
    return true;
}

extern volatile bool g_inAiMod;     // defined in RealtimeAiMod.cpp

bool touch_reaction_tick() {
#if defined(ARDUINO_M5STACK_Core2) || defined(ARDUINO_M5STACK_CORES3)
    if (!g_inAiMod || night_mode_display_is_off() || camera_is_busy()) return false;
    static int strokeAccum = 0;

    auto count = M5.Touch.getCount();
    if (!count) return false;
    auto t = M5.Touch.getDetail();
    int width = M5.Display.width();
    int height = M5.Display.height();
    if (width <= 0 || height <= 0) return false;

    if (t.wasPressed()) strokeAccum = 0;

    if (t.isPressed()) {
        if (g_lookEnabled) {
            float horizontal = ((float)t.x / (width * 0.5f)) - 1.0f;
            float vertical = ((float)t.y / (height * 0.5f)) - 1.0f;
            horizontal = constrain(horizontal, -1.0f, 1.0f);
            vertical = constrain(vertical, -1.0f, 1.0f);
            avatar.setGaze(vertical, horizontal);
            idle_motion_hold(1000);
        }
        if (g_strokeEnabled) strokeAccum += abs(t.deltaX()) + abs(t.deltaY());
    }

    if (t.wasReleased()) {
        bool petStroke = g_strokeEnabled
                         && !t.wasFlicked()
                         && strokeAccum > g_strokeThreshold;
        if (petStroke) {
            Serial.printf("[touch] stroke (%dpx) -> pet\n", strokeAccum);
            idle_talk_note_activity();
            bool fired = pet_reaction_fire();
            Serial.printf("[touch] pet release consumed (fired=%d)\n", fired ? 1 : 0);
            strokeAccum = 0;
            return true;
        }
        strokeAccum = 0;
    }
#endif
    return false;
}

void touch_reaction_init() {
    load_from_spiffs();
    Serial.println("[touch] ready (polled from loop)");
}
