#ifndef _REALTIME_LISTEN_STATE_H
#define _REALTIME_LISTEN_STATE_H

#include <stdint.h>

struct RealtimeStateSnapshot {
    bool recordRequested;
    bool sessionReady;
    bool recording;
    bool speaking;
    uint32_t queuedSinceMs;
};

struct RealtimeStateChange {
    bool started = false;
    bool paused = false;
};

// Pure state machine. RealtimeLLMBase supplies the cross-task lock and mirrors
// the speaking flag needed by the legacy LLMBase interface.
class RealtimeListenState {
public:
    RealtimeStateChange request(uint32_t nowMs) {
        RealtimeStateChange change;
        recordRequested = true;
        if (!sessionReady || speaking) {
            if (!speaking && !queueActive) {
                queuedSinceMs = nowMs;
                queueActive = true;
            }
        } else if (!recording) {
            recording = true;
            queuedSinceMs = 0;
            queueActive = false;
            change.started = true;
        }
        return change;
    }

    RealtimeStateChange cancel() {
        RealtimeStateChange change;
        change.paused = recording;
        recordRequested = false;
        recording = false;
        queuedSinceMs = 0;
        queueActive = false;
        return change;
    }

    RealtimeStateChange pause(bool preserveRequest, uint32_t nowMs) {
        RealtimeStateChange change;
        change.paused = recording;
        recording = false;
        recordRequested = preserveRequest && recordRequested;
        if (recordRequested && !sessionReady && !queueActive) {
            queuedSinceMs = nowMs;
            queueActive = true;
        } else if (!recordRequested || sessionReady) {
            queuedSinceMs = 0;
            queueActive = false;
        }
        return change;
    }

    RealtimeStateChange setSessionReady(bool ready, uint32_t nowMs) {
        RealtimeStateChange change;
        sessionReady = ready;
        if (!ready) {
            if (recording) {
                recording = false;
                recordRequested = true;
                change.paused = true;
            }
            if (recordRequested && !queueActive) {
                queuedSinceMs = nowMs;
                queueActive = true;
            }
        } else if (recordRequested && !recording && !speaking) {
            recording = true;
            queuedSinceMs = 0;
            queueActive = false;
            change.started = true;
        }
        return change;
    }

    RealtimeStateChange setSpeaking(bool value, bool resumeListening) {
        RealtimeStateChange change;
        speaking = value;
        if (value) {
            change.paused = recording;
            recording = false;
        } else if (resumeListening && recordRequested && sessionReady && !recording) {
            recording = true;
            queuedSinceMs = 0;
            queueActive = false;
            change.started = true;
        }
        return change;
    }

    bool expire(uint32_t nowMs, uint32_t timeoutMs) {
        if (recordRequested && !sessionReady && queueActive
            && (uint32_t)(nowMs - queuedSinceMs) >= timeoutMs) {
            cancel();
            return true;
        }
        return false;
    }

    RealtimeStateSnapshot snapshot() const {
        return {recordRequested, sessionReady, recording, speaking, queuedSinceMs};
    }

private:
    bool recordRequested = false;
    bool sessionReady = false;
    bool recording = false;
    bool speaking = false;
    uint32_t queuedSinceMs = 0;
    bool queueActive = false;
};

#endif
