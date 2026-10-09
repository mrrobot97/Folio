#!/usr/bin/env python3
"""Ensure the generated variable UI font covers every scanned interface codepoint."""
import importlib.util
from pathlib import Path
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("font_generator", root / "firmware/tools/gen_builtin_font.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
rendered = []
for item in generator.SCAN_DIRS:
    files = [item] if item.is_file() else [p for p in item.rglob("*") if p.suffix in {".c", ".h"}]
    for path in files:
        if path.suffix in {".c", ".h"}:
            rendered.extend(generator.unescape(m.group()) for m in generator.STRING_RE.finditer(path.read_text(errors="ignore")))
rendered.append((root / "firmware/main/assets/reading.md").read_text())
text = generator.unique_text(generator.ascii_printable(), generator.load_preset(generator.CHARSET), *rendered)
with TTFont(root / "firmware/main/assets/builtin.ttf") as font:
    missing = sorted(set(text) - set(map(chr,font.getBestCmap())))
    assert not missing, f"Missing UI characters: {''.join(missing)!r}; regenerate the font"
    assert "fvar" in font, "UI design requires a variable font with preserved weight"
    names = {record.toUnicode() for record in font["name"].names if record.nameID == 1}
    assert names == {"Read Pico UI"}, names
    assert any(record.nameID == 13 and "Open Font License" in record.toUnicode() for record in font["name"].names)
print(f"UI font: {len(text)} codepoints covered; variable weight, renamed family and OFL retained")
