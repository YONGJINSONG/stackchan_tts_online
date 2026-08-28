# Realtime server VAD for child pauses

## Purpose

Replace `semantic_vad` with `server_vad` so replies start after a silence timer, not a semantic end-of-turn guess. Tune for kids who pause mid-sentence: not as snappy as 400 ms, not as slow as waiting for semantic VAD.

## Scope

- `firmware/src/llm/ChatGPT/RealtimeChatGPT.cpp` `session.update` only.
- Keep `turn_detection` under `audio.input` (current GA session shape).
- Do not change mic/speaker handoff, barge-in, or `response.create` after function calls.

## Session values

```json
"turn_detection": {
  "type": "server_vad",
  "threshold": 0.5,
  "prefix_padding_ms": 300,
  "silence_duration_ms": 700,
  "create_response": true,
  "interrupt_response": false
}
```

- `prefix_padding_ms` 300: avoid clipping the first syllable (200 is tighter).
- `silence_duration_ms` 700: child pause room; 400 cuts too often.
- `create_response` true: matches current `input_audio_buffer.committed` → speaker path.
- `interrupt_response` false: mic is ended while speaking, so barge-in would not work and could race I2S.

## Verify

- Flash `m5stack-cores3-realtime-camera`.
- After `session.updated`, speak a short Korean sentence; the robot should answer without waiting as long as semantic VAD.
- Pause mid-sentence (~0.5 s) and continue; the reply should not start until ~700 ms of silence.
- If first sounds are clipped, raise `prefix_padding_ms` to 300+; if ambient noise false-triggers, raise `threshold`.
