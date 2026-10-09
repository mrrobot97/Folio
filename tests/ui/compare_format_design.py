#!/usr/bin/env python3
"""Build local original-size comparisons; never download or publish design assets."""
import html
import json
from pathlib import Path
import shutil
import struct
import sys

output = Path(sys.argv[1] if len(sys.argv) > 1 else '/tmp/folio-format-ui-host')
baseline = Path(sys.argv[2] if len(sys.argv) > 2 else '/Users/catfish/read_pico_firmware/docs/local/ui-redesign/v0.4.2/output/png')
pairs = [
    ('R04-book-root', 'R04'), ('R04-edited', 'R04-EDITED'), ('R04-defaults', 'R04-DEFAULT'), ('R04-defaults-edited', 'R04-DEFAULT-EDITED'),
    ('R14-size', 'R14'), ('FMT-recovered-field-candidate', 'R14'), ('R14-size-weight-candidate', 'R14'),
    ('R14-size-min-disabled', 'R14'), ('R14-size-max-disabled', 'R14'),
    ('R15-standard', 'R15'), ('R15-spacing', 'R15'), ('R14-static-weight-disabled', 'R14'), ('S02-default-entry', 'S02'), ('R11-gray', 'R11-GRAY'), ('R11-dark-confirm', 'R11-WARN'),
    ('R24-discard-confirm', 'R24'), ('R24-defaults-interrupted-field', 'R24'),
    ('FMT-ERROR-transaction', 'FMT-ERROR'), ('FMT-discard-failure', 'FMT-ERROR'),
    ('R10-font-picker', 'R10'), ('S05-font-error', 'S05-ERROR'),
    ('FONT-COVERAGE-builtin', 'FONT-COVERAGE'), ('FONT-child-checkpoint-error', 'FMT-CP-ERROR'),
    ('FONT-return-from-transfer', 'S05-DRAFT'), ('FMT-RESOURCE-MISSING', 'FMT-RESOURCE-MISSING'),
]
def dimensions(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not PNG: {path}')
    return struct.unpack('!II', data[16:24])

reference = output / 'design-reference'
reference.mkdir(exist_ok=True)
rows = []
manifest = []
for actual, design in pairs:
    a = output / (actual + '.png')
    b = baseline / (design + '.png')
    if not a.exists():
        continue
    assert dimensions(a) == (684, 1216), a
    assert dimensions(b) == (684, 1216), b
    shutil.copyfile(b, reference / b.name)
    manifest.append({'production': a.name, 'design': b.name, 'size': [684, 1216]})
    rows.append(f'<section><h2>{html.escape(actual)} / {html.escape(design)}</h2><div><figure><figcaption>设计参考 · v0.4.2</figcaption><img src="design-reference/{html.escape(b.name)}" width="684" height="1216"></figure><figure><figcaption>生产 UI 回调 + 真实字体引擎</figcaption><img src="{html.escape(a.name)}" width="684" height="1216"></figure></div></section>')
(output / 'comparison.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
batch=json.loads((output/'render-manifest.json').read_text())
(output / 'comparison.html').write_text('<!doctype html><meta charset="utf-8"><title>排版界面原尺寸比较</title><style>body{font:16px system-ui;background:#eee;margin:24px}section{margin-bottom:48px}section>div{display:flex;gap:16px}figure{margin:0}figcaption{padding:12px}img{display:block;max-width:none;background:white}</style><h1>排版界面原尺寸比较</h1><p>每张均为 684×1216；样例文件、字体、时间和候选值由测试 fixture 决定。比较不代表面板或全稿验收。</p>' + '<p>生成批次: '+html.escape(batch['generated_utc'])+' · 字体 SHA256: '+batch['font_sha256']+' · '+str(batch['font_bytes'])+' bytes</p>'+''.join(rows))
print(f'{len(manifest)} original-size design/production pairs: {output / "comparison.html"}')
