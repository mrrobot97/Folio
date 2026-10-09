#!/usr/bin/env python3
"""Analyze local Folio JSONL/ESP log captures; never connect to a device."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re
import sys

SCENARIOS = {"system", "home", "library", "reader", "format", "stats",
             "settings", "transfer", "sleep", "other"}
KINDS = {"session", "input", "input_end", "render", "display"}
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def integer(value, minimum=0):
    return type(value) is int and value >= minimum


def validate(record):
    if not isinstance(record, dict) or type(record.get("v")) is not int or record["v"] != 1:
        return "schema_version"
    kind = record.get("kind")
    if kind not in KINDS or not integer(record.get("seq"), 1):
        return "kind_or_sequence"
    if kind in {"session", "input", "input_end"}:
        if not integer(record.get("ts_us")):
            return "timestamp"
    if kind == "session":
        return None
    if record.get("scenario") not in SCENARIOS or not integer(record.get("input_id")):
        return "scenario_or_input_id"
    if kind == "input":
        if record["input_id"] != record["seq"] or record.get("input") not in {"touch", "key", "gesture", "power", "unknown"}:
            return "input"
    if kind == "input_end" and not record["input_id"]:
        return "input_end"
    if kind in {"render", "display"}:
        if not all(integer(record.get(k)) for k in ("begin_us", "end_us", "duration_us", "internal_free", "psram_free")):
            return "span_fields"
        if record["end_us"] - record["begin_us"] != record["duration_us"]:
            return "span_duration"
    if kind == "display":
        area = record.get("area")
        if not integer(record.get("mode")) or record.get("wave") not in {"unknown", "panel", "follow"}:
            return "display_mode"
        if not isinstance(area, list) or len(area) != 4 or not all(type(v) is int for v in area) or area[2] < 0 or area[3] < 0:
            return "display_area"
        if type(record.get("result")) is not int:
            return "display_result"
    return None


def distribution(values):
    """Nearest-rank percentiles; absent observations stay null, never zero."""
    values = sorted(values)
    return {"n": len(values), "p50": values[math.ceil(len(values) * .50) - 1] if values else None,
            "p95": values[math.ceil(len(values) * .95) - 1] if values else None,
            "min": values[0] if values else None, "max": values[-1] if values else None}


def analyze(lines, evidence="unverified"):
    quality = Counter()
    errors = []
    records = []
    sequences = defaultdict(set)
    session = 0
    for line_number, line in enumerate(lines, 1):
        quality["lines"] += 1
        line = ANSI.sub("", line).strip()
        if not line:
            quality["blank_lines"] += 1
            continue
        if not line.startswith("{"):
            marker = "folio_metrics: "
            if marker not in line:
                quality["ignored_lines"] += 1
                continue
            line = line.split(marker, 1)[1].strip()
        try:
            record = json.loads(line)
            reason = validate(record)
        except (ValueError, TypeError):
            reason = "invalid_json"
        if reason:
            quality["bad_lines"] += 1
            if len(errors) < 20:
                errors.append({"line": line_number, "reason": reason})
            continue
        if record["kind"] == "session":
            session += 1
            quality["session_markers"] += 1
        elif not session:
            quality["records_before_session"] += 1
        if record["seq"] in sequences[session]:
            quality["duplicate_sequences"] += 1
            continue
        sequences[session].add(record["seq"])
        record = dict(record, session=session)
        records.append(record)
    quality["valid_records"] = len(records)
    quality["sequence_gaps"] = sum(max(ids) - len(ids) for ids in sequences.values() if ids)
    inputs = {}
    ends = {}
    spans = defaultdict(list)
    groups = defaultdict(lambda: defaultdict(list))
    counters = defaultdict(Counter)
    for record in records:
        kind = record["kind"]
        if kind == "session":
            continue
        scenario = record["scenario"]
        key = (record["session"], record["input_id"])
        if kind == "input":
            inputs[key] = record
            counters[scenario]["input_count"] += 1
        elif kind == "input_end":
            if key in ends:
                quality["duplicate_input_ends"] += 1
            ends[key] = record
        else:
            groups[scenario][kind + "_us"].append(record["duration_us"])
            groups[scenario]["internal_free"].append(record["internal_free"])
            groups[scenario]["psram_free"].append(record["psram_free"])
            if record["input_id"]:
                spans[key].append(record)
            else:
                counters[scenario]["background_spans"] += 1
            if kind == "display":
                counters[scenario]["wave_transactions"] += 1
                counters[scenario]["gc16_transactions"] += (record["mode"] & 0xF) == 2
                counters[scenario]["display_errors"] += record["result"] != 0
                counters[scenario]["mode_0x%x" % (record["mode"] & 0xF)] += 1
    for key, end in ends.items():
        if key not in inputs:
            quality["orphan_input_ends"] += 1
        elif end["ts_us"] < inputs[key]["ts_us"]:
            quality["invalid_input_end_time"] += 1
    for key, rows in spans.items():
        if key not in inputs:
            quality["orphan_spans"] += len(rows)
    for key, event in inputs.items():
        scenario = event["scenario"]
        end = ends.get(key)
        rows = spans.get(key, [])
        valid = [r for r in rows if r["begin_us"] >= event["ts_us"]]
        quality["spans_before_input"] += len(rows) - len(valid)
        if end is None:
            quality["unclosed_inputs"] += 1
            counters[scenario]["unclosed_inputs"] += 1
        elif end["ts_us"] >= event["ts_us"]:
            groups[scenario]["input_handling_us"].append(end["ts_us"] - event["ts_us"])
        waves = [r for r in valid if r["kind"] == "display"]
        if not waves:
            counters[scenario]["inputs_without_display"] += 1
        else:
            groups[scenario]["input_to_first_display_begin_us"].append(min(r["begin_us"] for r in waves) - event["ts_us"])
            groups[scenario]["input_to_first_display_end_us"].append(min(r["end_us"] for r in waves) - event["ts_us"])
        # 完成样本才计每次输入的波形次数；异步结束仍保留。/ Only closed inputs enter per-input counts; async spans remain associated.
        if end is not None and end["ts_us"] >= event["ts_us"]:
            groups[scenario]["waves_per_closed_input"].append(len(waves))
            groups[scenario]["gc16_per_closed_input"].append(sum((r["mode"] & 0xF) == 2 for r in waves))
    result_groups = {}
    for scenario in sorted(groups.keys() | counters.keys()):
        group = groups[scenario]
        result_groups[scenario] = {
            **{name: counters[scenario][name] for name in ("input_count", "wave_transactions", "gc16_transactions",
               "display_errors", "background_spans", "unclosed_inputs", "inputs_without_display")},
            "modes": {k[5:]: v for k, v in sorted(counters[scenario].items()) if k.startswith("mode_")},
            **{name: distribution(group[name]) for name in ("render_us", "display_us", "input_handling_us",
               "input_to_first_display_begin_us", "input_to_first_display_end_us", "waves_per_closed_input", "gc16_per_closed_input")},
            "heap_samples": len(group["internal_free"]),
            "min_observed_internal_free_bytes": min(group["internal_free"], default=None),
            "min_observed_psram_free_bytes": min(group["psram_free"], default=None),
        }
    quality_names = ("lines", "blank_lines", "ignored_lines", "bad_lines", "valid_records", "session_markers",
                     "records_before_session", "duplicate_sequences", "sequence_gaps", "duplicate_input_ends",
                     "orphan_input_ends", "invalid_input_end_time", "orphan_spans", "spans_before_input", "unclosed_inputs")
    return {"schema": 1, "evidence": evidence, "percentile_method": "nearest-rank",
            "limitations": ["Software boundaries only; not optical latency or electrical current.",
                            "Heap minima are observed snapshots, not absolute free-memory minima.",
                            "Missing or errored records can bias all distributions; inspect quality and sample counts.",
                            "Display records count instrumented calls, not undocumented driver internals or optical refreshes."],
            "quality": {name: quality[name] for name in quality_names}, "bad_line_examples": errors,
            "scenarios": result_groups}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", help="Local capture path, or - for stdin")
    parser.add_argument("--evidence", choices=("unverified", "host-fixture", "hardware-capture"), default="unverified",
                        help="Operator-supplied origin label; never inferred as hardware from JSON")
    parser.add_argument("--output", type=Path, help="Optional JSON report path; otherwise stdout")
    args = parser.parse_args()
    if args.capture == "-":
        report = analyze(sys.stdin, args.evidence)
    else:
        with open(args.capture, encoding="utf-8", errors="replace") as stream:
            report = analyze(stream, args.evidence)
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)
    return 0 if report["quality"]["valid_records"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
