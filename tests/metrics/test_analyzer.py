#!/usr/bin/env python3
"""Synthetic and actual production-C host-stub output; no hardware measurements."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("analyze_metrics", ROOT / "scripts/analyze_metrics.py")
metrics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metrics)
CAPTURE = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None


def record(kind, seq, **fields):
    return dict(v=1, kind=kind, seq=seq, **fields)


def run(*records):
    return metrics.analyze([json.dumps(r) for r in records], "host-fixture")


class AnalyzerTests(unittest.TestCase):
    def test_production_c_capture(self):
        self.assertIsNotNone(CAPTURE, "Run via tests/metrics/run.sh")
        report = metrics.analyze(CAPTURE.read_text().splitlines(), "host-fixture")
        for key in ("bad_lines", "sequence_gaps", "orphan_spans", "unclosed_inputs", "duplicate_sequences"):
            self.assertEqual(report["quality"][key], 0, key)
        reader = report["scenarios"]["reader"]
        self.assertEqual(reader["display_us"], dict(n=2, p50=500, p95=600, min=500, max=600))
        self.assertEqual(reader["render_us"]["p95"], 100)
        self.assertEqual(reader["wave_transactions"], 2)
        self.assertEqual(reader["gc16_transactions"], 1)
        self.assertEqual(reader["display_errors"], 1)
        self.assertEqual(reader["input_to_first_display_end_us"]["p95"], 800)
        self.assertEqual(reader["min_observed_internal_free_bytes"], 100000)
        self.assertEqual(reader["min_observed_psram_free_bytes"], 6000000)
        self.assertEqual(report["scenarios"]["stats"]["inputs_without_display"], 2)
        self.assertEqual(report["scenarios"]["home"]["render_us"]["p50"], 300)
        self.assertEqual(report["scenarios"]["system"]["background_spans"], 1)

    def test_bad_lines_and_noise(self):
        report = metrics.analyze(["boot: normal", "", "{bad", "I (1) folio_metrics: {truncated",
                                  '{"v":1,"kind":[]}', "null", "\x1b[32mI (0) folio_metrics: " +
                                  json.dumps(record("session", 1, ts_us=0)) + "\x1b[0m"])
        self.assertEqual(report["quality"]["bad_lines"], 3)
        self.assertEqual(report["quality"]["ignored_lines"], 2)
        self.assertEqual(report["quality"]["session_markers"], 1)
        self.assertEqual(report["evidence"], "unverified")

    def test_validation(self):
        base = record("display", 2, begin_us=5, end_us=8, duration_us=3, input_id=0, scenario="stats",
                      internal_free=1, psram_free=0, area=[-1, 0, 20, 30], result=-1, mode=5, wave="panel")
        self.assertIsNone(metrics.validate(base))
        for key, value in (("v", True), ("seq", 0), ("input_id", -1), ("scenario", "book-name"),
                           ("duration_us", 4), ("end_us", 1), ("internal_free", None), ("psram_free", True),
                           ("area", [0, 0, -1, 1]), ("mode", -1), ("wave", "custom-path"), ("result", False)):
            with self.subTest(key=key):
                self.assertIsNotNone(metrics.validate(dict(base, **{key: value})))

    def test_missing_duplicate_and_reboot(self):
        event = record("input", 3, ts_us=2, input_id=3, scenario="reader", input="key")
        report = run(record("session", 1, ts_us=0), event, event,
                     record("session", 1, ts_us=0), record("input_end", 2, ts_us=7, input_id=3, scenario="reader"))
        self.assertEqual(report["quality"]["session_markers"], 2)
        self.assertEqual(report["quality"]["sequence_gaps"], 1)
        self.assertEqual(report["quality"]["duplicate_sequences"], 1)
        self.assertEqual(report["quality"]["unclosed_inputs"], 1)
        self.assertEqual(report["quality"]["orphan_input_ends"], 1)
        self.assertEqual(report["scenarios"]["reader"]["waves_per_closed_input"]["n"], 0)

    def test_out_of_order_and_invalid_correlation(self):
        display = record("display", 3, begin_us=5, end_us=10, duration_us=5, input_id=2,
                         scenario="reader", internal_free=0, psram_free=0, area=[0, 0, 1, 1], result=0, mode=2, wave="panel")
        report = run(record("session", 1, ts_us=0), display,
                     record("input", 2, ts_us=7, input_id=2, scenario="reader", input="key"),
                     record("input_end", 4, ts_us=6, input_id=2, scenario="reader"),
                     dict(display, seq=5, input_id=9))
        self.assertEqual(report["quality"]["spans_before_input"], 1)
        self.assertEqual(report["quality"]["orphan_spans"], 1)
        self.assertEqual(report["quality"]["invalid_input_end_time"], 1)
        self.assertEqual(report["quality"]["sequence_gaps"], 0)
        self.assertIsNone(report["scenarios"]["reader"]["input_to_first_display_end_us"]["p95"])

    def test_percentiles_and_empty(self):
        self.assertEqual(metrics.distribution(list(range(1, 101)))["p95"], 95)
        self.assertEqual(metrics.distribution([99])["p50"], 99)
        self.assertIsNone(metrics.distribution([])["p95"])
        self.assertEqual(metrics.analyze([])["scenarios"], {})

    def test_cli(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "report.json"
            subprocess.run([sys.executable, str(ROOT / "scripts/analyze_metrics.py"), str(CAPTURE),
                            "--evidence", "host-fixture", "--output", str(output)], check=True)
            self.assertEqual(json.loads(output.read_text())["evidence"], "host-fixture")
        empty = subprocess.run([sys.executable, str(ROOT / "scripts/analyze_metrics.py"), "-"],
                               input="", text=True, capture_output=True)
        self.assertEqual(empty.returncode, 2)


if __name__ == "__main__":
    unittest.main()
