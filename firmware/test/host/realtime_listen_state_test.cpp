#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/llm/RealtimeListenState.h"

static void assertInvariant(const RealtimeListenState& state) {
    auto s = state.snapshot();
    assert(!s.recording || (s.recordRequested && s.sessionReady && !s.speaking));
}

int main() {
    RealtimeListenState state;

    // Request before ready, then explicit second-touch cancellation.
    state.request(100);
    auto s = state.snapshot();
    assert(s.recordRequested && !s.recording && s.queuedSinceMs == 100);
    state.cancel();
    s = state.snapshot();
    assert(!s.recordRequested && !s.recording && s.queuedSinceMs == 0);

    // A queued request starts exactly once when the session becomes ready.
    state.request(200);
    assert(state.setSessionReady(true, 210).started);
    assert(!state.setSessionReady(true, 220).started);
    assertInvariant(state);

    // Disconnect pauses capture but preserves intent; ready resumes once.
    assert(state.setSessionReady(false, 300).paused);
    s = state.snapshot();
    assert(s.recordRequested && !s.recording && s.queuedSinceMs == 300);
    assert(state.setSessionReady(true, 310).started);
    assert(!state.setSessionReady(true, 320).started);
    assertInvariant(state);

    // Speaking always prevents recording and completion resumes listening.
    assert(state.setSpeaking(true, true).paused);
    s = state.snapshot();
    assert(s.speaking && !s.recording && s.recordRequested);
    assert(state.setSpeaking(false, true).started);
    assertInvariant(state);

    // Mode exit clears intent completely.
    state.cancel();
    s = state.snapshot();
    assert(!s.recordRequested && !s.recording);

    // Queue expiry clears busy state at 60 seconds, including uint32 wrap.
    state.setSessionReady(false, 0);
    state.request(1000);
    assert(!state.expire(60999, 60000));
    assert(state.expire(61000, 60000));
    state.request(UINT32_MAX - 100);
    assert(state.expire(59900, 60000));
    state.request(0);
    assert(state.expire(60000, 60000));
    assertInvariant(state);

    std::cout << "RealtimeListenState tests passed\n";
    return 0;
}
