#!/usr/bin/env python3
"""Fingerprint indexed firmware inputs using their current working-tree contents."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, help="Save the complete file manifest")
args = parser.parse_args()
paths = subprocess.check_output(["git", "ls-files", "-z", "firmware"], cwd=root).decode().split("\0")
entries = []
for name in sorted(filter(None, paths)):
    path = Path(name)
    if any(part in {"docs", "tools", ".github", ".devcontainer"} for part in path.parts):
        continue
    if path.name.startswith("sdkconfig") or path.name.startswith("Kconfig") or path.name in {"CMakeLists.txt", "dependencies.lock"} or path.suffix in {".c", ".h", ".S", ".cpp", ".cmake", ".yml", ".yaml", ".html", ".csv", ".bin", ".ttf"} or name == "firmware/main/assets/reading.md":
        data = (root / path).read_bytes()
        entries.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
digest = hashlib.sha256(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
summary = {"sha256": digest, "file_count": len(entries), "selection": "indexed firmware build/deployment inputs; source_fingerprint.py"}
if args.output:
    args.output.write_text(json.dumps({**summary, "files": entries}, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(summary))
