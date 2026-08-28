# Realtime audio transport follow-up

## Evidence

- The device completes `session.update`, then disconnects as soon as microphone capture begins.
- The session declared `audio/pcm` at 24000 Hz but microphone chunks were captured at 16000 Hz.

## Approved repair

1. Capture PCM at 24000 Hz to match the session format.
2. Use the current documented Realtime model, `gpt-realtime-2.1`, consistently in the WebSocket URL and `session.update` payload.
3. Log the outcome and size of audio append sends, and decode WebSocket close diagnostics when supplied by the peer.
4. Preserve a pending listen request while the socket is reconnecting; a second tap in that state must not cancel it.

## Verification

- Build and flash `m5stack-cores3-realtime-camera` to COM3.
- Tap once after `session.updated`. The monitor must show a successful audio append with `rate=24000`; the robot should answer.
- If it still closes, retain the new `error/close` or audio-send log to distinguish a local send failure from an API close reason.
