#include "MusicAction.h"
#include "driver/PlayMP3.h"
#include <Avatar.h>
#include <M5Unified.h>
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;
extern Avatar avatar;
extern Robot* robot;

static String g_path;
static String g_name;
static String g_result;
static volatile bool g_queued = false;
static volatile bool g_playing = false;
static volatile bool g_resultReady = false;

bool music_action_request_play(const char* path, const char* displayName) {
    if (!path || !path[0]) return false;
    if (g_queued || g_playing) {
        playMP3_request_stop();
    }
    g_path = path;
    g_name = displayName ? displayName : path;
    g_result = "";
    g_resultReady = false;
    g_queued = true;
    Serial.printf("[music] queued %s\n", g_path.c_str());
    return true;
}

void music_action_stop() {
    playMP3_request_stop();
}

bool music_action_is_playing() {
    return g_queued || g_playing || playMP3_is_running();
}

void music_action_process_pending() {
    if (!g_queued) return;
    g_queued = false;
    g_playing = true;
    avatar.setSpeechText("음악 재생 중 · 화면을 누르면 정지");
#if defined(REALTIME_API)
    if (robot && robot->llm) {
        RealtimeLLMBase* rt = (RealtimeLLMBase*)robot->llm;
        if (rt->isRealtimeRecording()) rt->pauseRealtimeRecord(true);
    }
    if (M5.Mic.isEnabled()) M5.Mic.end();
#endif
    Serial.printf("[music] play %s\n", g_path.c_str());
    bool ok = playMP3SD(g_path.c_str());
    bool stopped = playMP3_was_stopped();
    avatar.setSpeechText("");
    g_playing = false;
    if (stopped) {
        g_result = "{\"result\":\"음악을 멈췄어요.\"}";
    } else if (ok) {
        String esc = g_name;
        esc.replace("\"", "'");
        g_result = String("{\"result\":\"") + esc + " 를 재생했어요.\"}";
    } else {
        g_result = "{\"error\":\"음악을 재생하지 못했어요.\"}";
    }
    g_resultReady = true;
    Serial.printf("[music] done ok=%d stopped=%d\n", (int)ok, (int)stopped);
}

bool music_action_take_result(String& result) {
    if (!g_resultReady) return false;
    result = g_result;
    g_resultReady = false;
    return true;
}

void music_action_abandon() {
    playMP3_request_stop();
    g_queued = false;
    g_playing = false;
    g_resultReady = false;
}
