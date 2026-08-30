#include "BodyLight.h"

#include <M5Unified.h>
#include "drivers/PY32IOExpander_Class/PY32IOExpander_Class.hpp"

namespace {

constexpr uint8_t kLedCount = 12;
constexpr uint8_t kBrightness = 160;  // 0..255, keep body LEDs from blasting
constexpr uint8_t kRgbDataPin = 13;

struct NamedColor {
    const char* id;
    const char* ko;
    uint8_t r, g, b;
};

const NamedColor kPalette[] = {
    {"red",    "빨강", 255,  36,  36},
    {"orange", "주황", 255, 120,  20},
    {"yellow", "노랑", 255, 210,  36},
    {"green",  "초록",  40, 220,  72},
    {"cyan",   "하늘",  36, 200, 255},
    {"blue",   "파랑",  40,  80, 255},
    {"purple", "보라", 176,  56, 255},
    {"pink",   "분홍", 255,  80, 160},
    {"white",  "하양", 255, 255, 255},
};
constexpr int kPaletteN = (int)(sizeof(kPalette) / sizeof(kPalette[0]));

m5::PY32IOExpander_Class* g_py32 = nullptr;
bool g_available = false;
bool g_initedLeds = false;

volatile bool g_pending = false;
volatile bool g_wantOn = false;
volatile bool g_rainbow = false;
volatile uint8_t g_r = 255, g_g = 210, g_b = 36;
int g_cycle = 2;
String g_lastKo = "노랑";

uint8_t scale(uint8_t c) {
    return (uint16_t)c * kBrightness / 255;
}

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s == 0) {
        r = g = b = v;
        return;
    }
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

String lower_copy(const char* s) {
    String t = s ? String(s) : String();
    t.trim();
    t.toLowerCase();
    return t;
}

bool contains_ko(const String& hay, const char* needle) {
    return hay.indexOf(needle) >= 0;
}

int match_palette(const String& s) {
    if (s.length() == 0) return -2;
    if (s == "next" || s == "cycle" || s == "random" || s == "rand"
        || contains_ko(s, "다음") || contains_ko(s, "바꿔") || contains_ko(s, "다른")
        || contains_ko(s, "랜덤")) {
        return -2;
    }
    if (s == "rainbow" || contains_ko(s, "무지개")) return -1;
    for (int i = 0; i < kPaletteN; i++) {
        if (s == kPalette[i].id) return i;
        if (contains_ko(s, kPalette[i].ko)) return i;
    }
    if (contains_ko(s, "빨간") || contains_ko(s, "레드")) return 0;
    if (contains_ko(s, "노란") || contains_ko(s, "옐로")) return 2;
    if (contains_ko(s, "초록") || contains_ko(s, "그린")) return 3;
    if (contains_ko(s, "파란") || contains_ko(s, "블루")) return 5;
    if (contains_ko(s, "보라") || contains_ko(s, "퍼플")) return 6;
    if (contains_ko(s, "분홍") || contains_ko(s, "핑크")) return 7;
    if (contains_ko(s, "흰") || contains_ko(s, "화이트")) return 8;
    if (contains_ko(s, "청록") || contains_ko(s, "하늘")) return 4;
    return -3;
}

void apply_now(bool on, bool rainbow, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_py32 || !g_available) return;
    if (!g_initedLeds) return;

    if (!on) {
        for (int i = 0; i < kLedCount; i++) {
            g_py32->setLedColor(i, 0, 0, 0);
        }
    } else if (rainbow) {
        for (int i = 0; i < kLedCount; i++) {
            uint8_t rr, gg, bb;
            hsv_to_rgb((uint8_t)(i * (256 / kLedCount)), 255, 255, rr, gg, bb);
            rr = scale(rr); gg = scale(gg); bb = scale(bb);
            g_py32->setLedColor(i, rr, gg, bb);
        }
    } else {
        uint8_t rr = scale(r), gg = scale(g), bb = scale(b);
        for (int i = 0; i < kLedCount; i++) {
            g_py32->setLedColor(i, rr, gg, bb);
        }
    }
    g_py32->refreshLeds();
}

}  // namespace

void body_light_init() {
    g_py32 = new m5::PY32IOExpander_Class(0x6F, &M5.In_I2C);

    // PY32 can become available after the main board has started. Match the
    // official StackChan startup sequence instead of treating one read as a
    // complete peripheral initialization.
    const uint32_t started = millis();
    while (!g_py32->begin() && millis() - started < 1200) {
        delay(200);
    }

    uint8_t ver = g_py32->readVersion();
    if (ver == 0 || ver == 0xFF) {
        Serial.printf("[light] PY32 init failed ver=0x%02X\n", ver);
        g_available = false;
        return;
    }

    // GPIO13 is the PY32 NeoPixel output. Without this pin setup, LED RAM
    // accepts I2C writes and logs success but the 12 body LEDs receive no data.
    g_py32->setDirection(kRgbDataPin, true);
    g_py32->setPullMode(kRgbDataPin, true);
    g_py32->setDriveMode(kRgbDataPin, false);
    g_py32->setLedCount(kLedCount);
    delay(200);

    g_available = true;
    g_initedLeds = true;
    Serial.printf("[light] PY32 ok ver=0x%02X RGB pin=%u LEDs=%u\n",
                  ver, (unsigned)kRgbDataPin, (unsigned)kLedCount);

    // A pair of black frames resets the WS2812 chain into a known state before
    // the first requested color, matching the official StackChan firmware.
    apply_now(false, false, 0, 0, 0);
    delay(50);
    apply_now(false, false, 0, 0, 0);
}

void body_light_tick() {
    if (!g_pending || !g_available) return;
    bool on = g_wantOn;
    bool rainbow = g_rainbow;
    uint8_t r = g_r, g = g_g, b = g_b;
    g_pending = false;
    apply_now(on, rainbow, r, g, b);
    Serial.printf("[light] applied on=%d rainbow=%d rgb=%u,%u,%u\n",
                  (int)on, (int)rainbow, (unsigned)r, (unsigned)g, (unsigned)b);
}

bool body_light_available() {
    return g_available;
}

String body_light_request(bool hasOn, bool on, const char* color) {
    if (!g_available) {
        return "{\"error\":\"본체 라이트를 찾지 못했어요.\"}";
    }

    String spec = lower_copy(color);
    bool turnOff = hasOn && !on && spec.length() == 0;
    if (turnOff) {
        g_wantOn = false;
        g_rainbow = false;
        g_pending = true;
        return "{\"result\":\"불을 껐어요.\"}";
    }

    int idx = match_palette(spec);
    if (idx == -3 && spec.length() > 0) {
        idx = -2;
    }
    if (idx == -1) {
        g_rainbow = true;
        g_wantOn = true;
        g_lastKo = "무지개";
        g_pending = true;
        return "{\"result\":\"무지개로 켰어요.\"}";
    }
    if (idx == -2) {
        bool justOn = hasOn && on && spec.length() == 0;
        if (!justOn) {
            g_cycle = (g_cycle + 1) % kPaletteN;
        }
        idx = g_cycle;
    } else {
        g_cycle = idx;
    }
    g_r = kPalette[idx].r;
    g_g = kPalette[idx].g;
    g_b = kPalette[idx].b;
    g_rainbow = false;
    g_wantOn = true;
    g_lastKo = kPalette[idx].ko;
    g_pending = true;
    String msg = String(kPalette[idx].ko) + "으로 켰어요.";
    return String("{\"result\":\"") + msg + "\"}";
}
