# CoreS3 OpenClaw agent bridge

## Purpose

Connect the existing CoreS3 conversation flow to the PC-side OpenClaw bridge without replacing the current ChatGPT/Realtime LLM or affecting local robot functions. Only diary, PC-side long-term memory, multi-step search, and shopping comparison requests are delegated through the new `agent_task` function.

## Scope

- Store all bridge connection and authentication settings under `agentBridge` in SD `/yaml/SC_SecConfig.yaml`; keep `/app/AiStackChanEx/SC_ExConfig.yaml` responsible for the existing LLM/TTS/STT settings only.
- Extend `StackchanExConfig` with safe secret loading that preserves Wi-Fi/API-key behavior, populates `llm.agentBridge`, and never prints secret values or the raw secret YAML.
- Add an Agent Bridge client that posts `profile`, `device_id`, `action`, and `text` as JSON to `/v1/agent` with the `X-Stackchan-Key` header.
- Add synchronous calls for non-Realtime builds and a single-flight asynchronous request/result path for Realtime builds so the WebSocket task is not blocked.
- Add and disambiguate the shared Function Calling schema while leaving local movement, light, camera, music, study, timer, and other device tools on their current paths.

## Main files

- `src/StackchanExConfig.*` and the startup configuration flow
- `src/agent/AgentBridgeClient.*`
- `src/llm/ChatGPT/FunctionCall.*` and Realtime deferred-result handling
- `../Copy-to-SD/yaml/SC_SecConfig.yaml`, `doc/fw_design.md`, and user-facing setup documentation

## Implementation notes

- Defaults are disabled, empty host/key, port `8765`, profile `kids`, and device ID `roni`. Reject profiles other than `kids` and `adult`.
- Use the PC LAN IPv4 address, never `127.0.0.1`. The CoreS3 contains only the bridge key; the OpenClaw token remains on the PC.
- Use a 5-second connect timeout and a 60-second response timeout. Do not log the authentication header, request text, diary/memory content, or response body.
- Accept HTTP 200 with JSON `{ "text": "..." }`; turn disabled Wi-Fi/configuration, authentication, transport, timeout, malformed JSON, and empty responses into bounded Korean error results.
- Realtime permits only one deferred Agent request. Disconnect/abandon invalidates the outstanding generation so a late result cannot be delivered to a newer tool call.
- Preserve unrelated user-owned files and current uncommitted work.

## Validation

- Build `m5stack-cores3-realtime-camera` and `m5stack-cores3`.
- Confirm serial output contains no Wi-Fi password, API key, Bridge key, raw secret YAML, diary text, or Bridge response body.
- Exercise valid `kids` routing and disabled, missing-host/key, invalid-profile, Wi-Fi-down, unreachable, authentication, non-200, timeout, malformed-JSON, and empty-text paths.
- On CoreS3, verify a long Agent request does not stall the Realtime WebSocket and its completed result produces the normal follow-up response.
- While Realtime is reconnecting, repeated taps must preserve the queued listen request. Taps during response audio must not contend for I2S, and proactive battery/idle/touch messages must remain queued until the session is ready and user recording is idle.
- Confirm dance, lights, photo, music, study, timer, and simple local search/memory behavior remain on their existing implementations.

## Rollback

Set `agentBridge.enabled: false` or remove the `agentBridge` block to disable delegation without changing the selected LLM. The implementation remains isolated behind `agent_task`, so removing its schema/handler and the bridge client restores the previous behavior.
