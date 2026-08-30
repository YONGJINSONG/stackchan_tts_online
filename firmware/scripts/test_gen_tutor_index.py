#!/usr/bin/env python3

import json
import os
import pathlib
import tempfile
import unittest
from unittest import mock

import gen_tutor_index as qidx


class TutorIndexTests(unittest.TestCase):
    def write_records(self, path: pathlib.Path) -> None:
        records = [
            {"id": "EN-1", "category": "english", "level": 1, "question": "가"},
            {"id": "SO-2", "category": "math_somamath", "level_num": 2},
            {"id": "FA-3", "category": "math_facto", "level": 3},
        ]
        path.write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in records),
                        encoding="utf-8")

    def test_generate_and_verify(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "curriculum.ndjson"
            self.write_records(source)
            count = qidx.generate_one(source)
            self.assertEqual(count, 3)
            self.assertTrue(source.with_suffix(".idx").exists())
            valid, reason = qidx.verify_qidx(source.with_suffix(".qidx"), source)
            self.assertEqual((valid, reason), (True, "ok"))

    def test_same_size_source_change_is_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "english.ndjson"
            self.write_records(source)
            qidx.generate_one(source)
            original_stat = source.stat()
            original = source.read_bytes()
            changed = original.replace(b'"EN-1"', b'"EN-9"', 1)
            self.assertEqual(len(original), len(changed))
            source.write_bytes(changed)
            os.utime(source, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
            valid, _ = qidx.verify_qidx(source.with_suffix(".qidx"), source)
            self.assertFalse(valid)

    def test_corrupt_entry_crc_is_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "math.ndjson"
            self.write_records(source)
            qidx.generate_one(source)
            cache = source.with_suffix(".qidx")
            raw = bytearray(cache.read_bytes())
            raw[-1] ^= 0x80
            cache.write_bytes(raw)
            valid, reason = qidx.verify_qidx(cache, source)
            self.assertEqual((valid, reason), (False, "entry CRC mismatch"))

    def test_added_question_invalidates_then_regenerates(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "english.ndjson"
            self.write_records(source)
            qidx.generate_one(source)
            with source.open("a", encoding="utf-8") as output:
                output.write(json.dumps({"id": "EN-4", "category": "english", "level": 2}) + "\n")
            valid, reason = qidx.verify_qidx(source.with_suffix(".qidx"), source)
            self.assertEqual((valid, reason), (False, "source changed"))
            self.assertEqual(qidx.generate_one(source), 4)
            self.assertEqual(qidx.verify_qidx(source.with_suffix(".qidx"), source),
                             (True, "ok"))

    def test_invalid_json_does_not_replace_existing_indexes(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "bad.ndjson"
            source.write_text('{"id":"OK"}\nnot-json\n', encoding="utf-8")
            idx = source.with_suffix(".idx")
            cache = source.with_suffix(".qidx")
            idx.write_bytes(b"old idx")
            cache.write_bytes(b"old qidx")
            with self.assertRaises(ValueError):
                qidx.generate_one(source)
            self.assertEqual(idx.read_bytes(), b"old idx")
            self.assertEqual(cache.read_bytes(), b"old qidx")

    def test_backup_failure_keeps_existing_indexes_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "english.ndjson"
            self.write_records(source)
            original_source = source.read_bytes()
            idx = source.with_suffix(".idx")
            cache = source.with_suffix(".qidx")
            idx.write_bytes(b"old idx")
            cache.write_bytes(b"old qidx")
            real_replace = os.replace

            def fail_second_backup(src, dst):
                if pathlib.Path(src) == cache and pathlib.Path(dst) == pathlib.Path(str(cache) + ".bak"):
                    raise PermissionError("simulated read-only media")
                return real_replace(src, dst)

            legacy, binary = qidx.build_artifacts(source)
            with mock.patch.object(qidx.os, "replace", side_effect=fail_second_backup):
                with self.assertRaises(PermissionError):
                    qidx.replace_pair(source, legacy, binary)
            self.assertEqual(source.read_bytes(), original_source)
            self.assertEqual(idx.read_bytes(), b"old idx")
            self.assertEqual(cache.read_bytes(), b"old qidx")


if __name__ == "__main__":
    unittest.main()
