# Kids Tutor qidx metadata cache

## Approved behavior

- Keep legacy decimal-offset `.idx` files unchanged and add a binary `.qidx`
  sidecar containing offset, level, ID hash, and category hash metadata.
- Prefer a valid `.qidx` during `QuestionDB::begin()` so normal startup does
  not parse every NDJSON record.
- If the cache is absent, stale, or corrupt, scan the NDJSON once in file
  order, keep the resulting metadata in RAM, and safely write a replacement
  through temporary and backup files. A write failure must not prevent use of
  the in-memory metadata.
- Generate adaptive curriculum pools from cached metadata. Read and fully
  parse NDJSON only when an actual question is selected.
- Add a standard-library-only PC tool that regenerates both legacy `.idx` and
  `.qidx` files directly in a mounted SD database directory.

## qidx v1 format

- Little-endian fixed-width binary data.
- Header fields: magic `QIDX`, version, header size, entry size, source byte
  size, source modification time, four independent distributed source-sample
  CRC32 values, record count, and entry-data CRC32.
- Each 16-byte entry contains `offset`, FNV-1a ID hash, FNV-1a category hash,
  `level`, and three reserved zero bytes.
- Validate the exact file length, header fields, source identity, entry CRC,
  monotonically increasing in-range offsets, and nonzero record count.

## Compatibility and validation

- Retain the existing public question-selection behavior and legacy `.idx`.
- Store the new category-hash vector with the existing PSRAM-first vectors.
- Preserve all unrelated Gesture, IdleMotion, Realtime, camera, music, and
  Kids Tutor worktree changes.
- Build `m5stack-cores3-realtime-camera`, `m5stack-cores3-realtime`, and
  `m5stack-core2-realtime` after static/tool tests pass.
