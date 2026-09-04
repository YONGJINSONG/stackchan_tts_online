# AgentBridge CoreS3 TLS PSRAM Allocator

## Purpose

CoreS3 Realtime keeps its OpenAI WebSocket TLS session alive while an
AgentBridge HTTPS request is deferred. ESP-IDF's default configuration sends
all mbedTLS allocations to internal RAM, so the second connection can fail in
`mbedtls_ssl_setup()` with `-32512` even while PSRAM is mostly unused.

## Decision

- Only CoreS3 overrides `esp_mbedtls_mem_calloc` and `esp_mbedtls_mem_free`.
- Allocations smaller than 4 KiB remain internal. Larger mbedTLS allocations
  prefer PSRAM and only fall back to internal RAM if PSRAM cannot satisfy them.
- Certificate validation remains required; `setInsecure()` and HTTP fallback
  are not allowed.
- AgentBridge emits redacted memory capacity and allocator-placement counters
  around each HTTPS request. It reports `-32512` as an HTTPS memory shortage,
  not as a certificate or clock problem.

## Verification

Build both CoreS3 Realtime targets, then make five consecutive Funnel bridge
requests. Each must complete without `-32512`, preserve the Realtime follow-up
response, and show large TLS allocations using PSRAM.
