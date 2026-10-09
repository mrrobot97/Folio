#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="${1:-/tmp/folio-ui-host}"
mkdir -p "$out"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itests/ui/stubs -Ifirmware/main/app -Ifirmware/main/ui -Ifirmware/main/font -Ifirmware/main/folio -Ifirmware/main/book -Ifirmware/main \
  tests/ui/ui_host_test.c firmware/main/folio/folio_ui.c firmware/main/apps/app_folio_home.c \
  firmware/main/apps/app_folio_stats.c firmware/main/apps/app_folio_settings.c \
  firmware/main/apps/app_folio_sleep.c firmware/main/apps/app_folio_storage.c -lm -o "$out/ui-host-test"
"$out/ui-host-test" firmware/sdcard/fonts/ChillDuanSansVF.ttf "$out"
python3 - "$out" <<'PY'
import pathlib, struct, sys, zlib
for p in pathlib.Path(sys.argv[1]).glob('*.pgm'):
    magic, dims, depth, raw = p.read_bytes().split(b'\n', 3)
    w,h=map(int,dims.split())
    def chunk(kind,data):
        return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+raw[y*w:(y+1)*w] for y in range(h))
    p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
PY
