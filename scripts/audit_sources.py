#!/usr/bin/env python3
"""Report upstream reuse, changes and omissions without touching the source checkout."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "docs/upstream-manifest.json").read_text())
report = {key: [] for key in ("unchanged", "modified", "missing", "new")}
original = {entry["path"] for entry in manifest["files"]}
for entry in manifest["files"]:
    path = ROOT / "firmware" / entry["path"]
    kind = "missing" if not path.is_file() else (
        "unchanged" if hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"] else "modified")
    report[kind].append(entry["path"])
for path in (ROOT / "firmware").rglob("*"):
    relative = path.relative_to(ROOT / "firmware")
    if any(part in {"build", "managed_components", "__pycache__", ".cache", "local"} for part in relative.parts):
        continue
    if path.is_file() and str(relative) not in original and path.name not in {"sdkconfig", "sdkconfig.old", "HANDOFF.local.md"}:
        report["new"].append(str(relative))
print(json.dumps({"upstream": manifest["commit"], "counts": {k: len(v) for k, v in report.items()},
                  "files": {k: sorted(v) for k, v in report.items() if k != "unchanged"}}, ensure_ascii=False, indent=2))
raise SystemExit(bool(report["missing"]))
