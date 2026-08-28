# Realtime voice regression rollback

## Evidence

- With the compatibility update, each `input_audio_buffer.append` send succeeds, but the server emits neither speech detection nor response events.
- The user confirms the previous-day build replied to voice input.

## Repair

- Restore the CoreS3 microphone capture rate to its known-working 16000 Hz.
- Restore the known-working `gpt-realtime` model identifier.
- Retain the reconnect state handling and compact transmission/close diagnostics.
- Correct the diagnostic format strings so serial output uses real line breaks.

## Verification

- Build and flash the CoreS3 camera realtime environment to COM3.
- After one touch, speak normally and confirm a response is received.
