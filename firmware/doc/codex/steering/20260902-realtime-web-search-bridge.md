# Realtime web search through the OpenClaw bridge

## Purpose

Keep HTTPS and JSON search work off the ChatGPT Realtime WebSocket task by
delegating `web_search` to the existing 12 KB `agent_bridge` worker.

## Scope

- Route `web_search` through `AgentBridgeClient::start()` only when
  `FunctionCall` was constructed with deferred Agent Bridge handling enabled.
- Keep non-Realtime Cascade search on the existing Brave Search implementation.
- Keep Gemini Live behavior unchanged.
- Reuse the existing single-flight deferred-result and follow-up path.
- Do not fall back to Brave Search from ChatGPT Realtime when the PC Bridge is
  unavailable.

## Implementation notes

- Use the `_deferAgentBridge` runtime flag rather than the `REALTIME_API` build
  macro so the behavior is limited to ChatGPT Realtime.
- Send action `search` and the original query as the Bridge request text.
- Redact deferred search arguments and all Bridge secrets and response bodies
  from Serial output.
- Include the HTTPClient transport error description and configured endpoint in
  connection diagnostics without logging request content or credentials.

## Validation

- Build `m5stack-cores3-realtime-camera` and `m5stack-cores3`.
- Confirm Realtime `web_search` is deferred and completes through the normal
  function-call follow-up path.
- Confirm an unavailable Bridge returns a bounded error without running Brave
  HTTPS on the WebSocket task.
- Confirm Cascade still uses Brave Search directly.

## Rollback

Restore the direct `web_search()` call in `FunctionCall::exec_calledFunc()` and
remove the related documentation updates. The Agent Bridge configuration and
`agent_task` behavior remain independent.
