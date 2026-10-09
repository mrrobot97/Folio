#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="${1:-/tmp/folio-font-host}"
mkdir -p "$out"
python3 - "$out" <<'PY'
import pathlib,platform,sys
out=pathlib.Path(sys.argv[1]);font=pathlib.Path('firmware/main/assets/builtin.ttf').resolve()
section='.section __TEXT,__const' if platform.system()=='Darwin' else '.section .rodata'
(out/'builtin.S').write_text(f'{section}\n.globl _binary_builtin_ttf_start\n.globl _binary_builtin_ttf_end\n_binary_builtin_ttf_start:\n.incbin "{font}"\n_binary_builtin_ttf_end:\n')
PY
cc -std=gnu11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g \
  -Itests/fonts/stubs -Ifirmware/main/font -Ifirmware/main \
  tests/fonts/font_host_test.c firmware/main/font/ttf_font.c "$out/builtin.S" -lm -lpthread -o "$out/font-host-test"
"$out/font-host-test" firmware/sdcard/fonts/ChillDuanSansVF.ttf "$out"
python3 - "$out" <<'PY'
import pathlib,struct,sys,zlib
for p in pathlib.Path(sys.argv[1]).glob('*.pgm'):
    magic,dims,depth,raw=p.read_bytes().split(b'\n',3);w,h=map(int,dims.split())
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+raw[y*w:(y+1)*w] for y in range(h))
    p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
PY
