#!/usr/bin/env python3
"""Generate legacy .idx and fast Kids Tutor .qidx metadata indexes."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import struct
import sys
import zlib


MAGIC = b"QIDX"
VERSION = 1
FLAGS = 0
SAMPLE_WINDOW_BYTES = 256
HEADER = struct.Struct("<4sHHHHIQIIIIIII")
ENTRY = struct.Struct("<IIIB3x")


def fnv1a(text: str) -> int:
    value = 2166136261
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def source_sample_crcs(path: pathlib.Path) -> tuple[int, int, int, int]:
    size = path.stat().st_size
    if size <= 0 or size > 0xFFFFFFFF:
        raise ValueError(f"unsupported source size: {size}")
    offsets = (0, size // 3, (size * 2) // 3, max(0, size - SAMPLE_WINDOW_BYTES))
    crcs: list[int] = []
    with path.open("rb") as source:
        for offset in offsets:
            source.seek(offset)
            data = source.read(min(SAMPLE_WINDOW_BYTES, size - offset))
            crcs.append(zlib.crc32(data) & 0xFFFFFFFF)
    return tuple(crcs)


def parse_level(record: dict) -> int:
    raw = record.get("level")
    if not isinstance(raw, int) or isinstance(raw, bool):
        raw = record.get("level_num", 1)
    if not isinstance(raw, int) or isinstance(raw, bool):
        raw = 1
    level = raw & 0xFF
    return level or 1


def scan_ndjson(path: pathlib.Path) -> tuple[list[int], bytes]:
    offsets: list[int] = []
    packed_entries: list[bytes] = []
    with path.open("rb") as source:
        line_number = 0
        while True:
            offset = source.tell()
            raw = source.readline()
            if not raw:
                break
            line_number += 1
            stripped = raw.strip()
            if len(stripped) <= 2:
                continue
            try:
                record = json.loads(stripped.decode("utf-8-sig"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ValueError(f"{path.name}:{line_number}: invalid JSON: {exc}") from exc
            if not isinstance(record, dict):
                raise ValueError(f"{path.name}:{line_number}: record is not an object")
            record_id = record.get("id", "")
            if not isinstance(record_id, str) or not record_id:
                raise ValueError(f"{path.name}:{line_number}: missing non-empty id")
            category = record.get("category", "")
            if not isinstance(category, str):
                category = ""
            if offset > 0xFFFFFFFF:
                raise ValueError(f"{path.name}:{line_number}: offset exceeds uint32")
            offsets.append(offset)
            packed_entries.append(
                ENTRY.pack(
                    offset,
                    fnv1a(record_id),
                    fnv1a(category) if category else 0,
                    parse_level(record),
                )
            )
    if not offsets:
        raise ValueError(f"{path.name}: no question records")
    return offsets, b"".join(packed_entries)


def build_artifacts(path: pathlib.Path) -> tuple[bytes, bytes]:
    offsets, entries = scan_ndjson(path)
    stat = path.stat()
    header = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        ENTRY.size,
        FLAGS,
        stat.st_size,
        int(stat.st_mtime),
        *source_sample_crcs(path),
        len(offsets),
        zlib.crc32(entries) & 0xFFFFFFFF,
        0,
    )
    legacy = "".join(f"{offset}\n" for offset in offsets).encode("ascii")
    return legacy, header + entries


def verify_qidx(qidx_path: pathlib.Path, source_path: pathlib.Path) -> tuple[bool, str]:
    try:
        raw = qidx_path.read_bytes()
        if len(raw) < HEADER.size:
            return False, "short header"
        fields = HEADER.unpack_from(raw)
        (magic, version, header_size, entry_size, flags, source_size,
         source_mtime, sample_crc_0, sample_crc_1, sample_crc_2, sample_crc_3,
         count, entries_crc, reserved) = fields
        if (magic != MAGIC or version != VERSION or header_size != HEADER.size or
                entry_size != ENTRY.size or flags != FLAGS or reserved != 0):
            return False, "format mismatch"
        stat = source_path.stat()
        sample_crcs = (sample_crc_0, sample_crc_1, sample_crc_2, sample_crc_3)
        if (source_size != stat.st_size or source_mtime != int(stat.st_mtime) or
                sample_crcs != source_sample_crcs(source_path)):
            return False, "source changed"
        if count == 0 or len(raw) != HEADER.size + count * ENTRY.size:
            return False, "length mismatch"
        entries = raw[HEADER.size:]
        if (zlib.crc32(entries) & 0xFFFFFFFF) != entries_crc:
            return False, "entry CRC mismatch"
        previous = -1
        for index in range(count):
            offset, _id_hash, _category_hash, _level = ENTRY.unpack_from(
                entries, index * ENTRY.size
            )
            if offset >= source_size or offset <= previous:
                return False, "invalid offset"
            previous = offset
        return True, "ok"
    except (OSError, struct.error, ValueError) as exc:
        return False, str(exc)


def write_fsynced(path: pathlib.Path, data: bytes) -> None:
    with path.open("wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def replace_pair(source_path: pathlib.Path, legacy: bytes, qidx: bytes) -> None:
    idx_path = source_path.with_suffix(".idx")
    qidx_path = source_path.with_suffix(".qidx")
    idx_tmp = pathlib.Path(str(idx_path) + ".tmp")
    qidx_tmp = pathlib.Path(str(qidx_path) + ".tmp")
    idx_backup = pathlib.Path(str(idx_path) + ".bak")
    qidx_backup = pathlib.Path(str(qidx_path) + ".bak")

    for temporary in (idx_tmp, qidx_tmp):
        temporary.unlink(missing_ok=True)
    write_fsynced(idx_tmp, legacy)
    write_fsynced(qidx_tmp, qidx)
    valid, reason = verify_qidx(qidx_tmp, source_path)
    if not valid:
        idx_tmp.unlink(missing_ok=True)
        qidx_tmp.unlink(missing_ok=True)
        raise ValueError(f"generated qidx failed verification: {reason}")

    for backup in (idx_backup, qidx_backup):
        backup.unlink(missing_ok=True)
    had_idx = idx_path.exists()
    had_qidx = qidx_path.exists()
    moved_idx = False
    moved_qidx = False
    installed_idx = False
    installed_qidx = False
    try:
        if had_idx:
            os.replace(idx_path, idx_backup)
            moved_idx = True
        if had_qidx:
            os.replace(qidx_path, qidx_backup)
            moved_qidx = True
        os.replace(idx_tmp, idx_path)
        installed_idx = True
        os.replace(qidx_tmp, qidx_path)
        installed_qidx = True
    except Exception:
        if installed_idx:
            idx_path.unlink(missing_ok=True)
        if installed_qidx:
            qidx_path.unlink(missing_ok=True)
        if moved_idx and idx_backup.exists():
            os.replace(idx_backup, idx_path)
        if moved_qidx and qidx_backup.exists():
            os.replace(qidx_backup, qidx_path)
        raise
    finally:
        idx_tmp.unlink(missing_ok=True)
        qidx_tmp.unlink(missing_ok=True)
    idx_backup.unlink(missing_ok=True)
    qidx_backup.unlink(missing_ok=True)


def generate_one(source_path: pathlib.Path) -> int:
    legacy, qidx = build_artifacts(source_path)
    replace_pair(source_path, legacy, qidx)
    return len(qidx[HEADER.size:]) // ENTRY.size


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate Kids Tutor .idx and .qidx files in a mounted SD database directory."
    )
    parser.add_argument("--db-dir", required=True, type=pathlib.Path)
    parser.add_argument(
        "--check", action="store_true",
        help="validate existing qidx files without replacing indexes",
    )
    args = parser.parse_args()
    db_dir = args.db_dir.resolve()
    sources = sorted(db_dir.glob("*.ndjson")) if db_dir.is_dir() else []
    if not sources:
        print(f"error: no .ndjson files in {db_dir}", file=sys.stderr)
        return 2

    failures = 0
    for source in sources:
        try:
            if args.check:
                valid, reason = verify_qidx(source.with_suffix(".qidx"), source)
                if not valid:
                    raise ValueError(reason)
                _, entries = scan_ndjson(source)
                print(f"OK {source.name}: {len(entries) // ENTRY.size} records")
            else:
                count = generate_one(source)
                print(f"WROTE {source.with_suffix('.idx').name} + "
                      f"{source.with_suffix('.qidx').name}: {count} records")
        except (OSError, ValueError) as exc:
            failures += 1
            print(f"ERROR {source.name}: {exc}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
