#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="${1:-$(mktemp -d /tmp/folio-reader-device.XXXXXX)}"
mkdir -p "$out"
out=$(cd "$out" && pwd)
state=$(mktemp -d "$out/state.XXXXXX")
python3 tests/reader/extract_device_callbacks.py "$out/reader_production.inc"
python3 - "$out" <<'PY'
import pathlib,platform,sys
out=pathlib.Path(sys.argv[1]);font=pathlib.Path('firmware/main/assets/builtin.ttf').resolve()
section='.section __TEXT,__const' if platform.system()=='Darwin' else '.section .rodata'
(out/'builtin.S').write_text(f'{section}\n.globl _binary_builtin_ttf_start\n.globl _binary_builtin_ttf_end\n_binary_builtin_ttf_start:\n.incbin "{font}"\n_binary_builtin_ttf_end:\n')
PY
flags='-std=gnu11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g'
cc $flags -DFORMAT_DIRECTORY=\""$state"\" -I"$out" -Itests/reader/device_stubs -Itests/ui/format_stubs -Itests/ui/stubs -Itests/fonts/stubs \
 -Ifirmware/main/app -Ifirmware/main/ui -Ifirmware/main/font -Ifirmware/main/folio -Ifirmware/main/book -Ifirmware/main \
 tests/reader/format_device_host_test.c firmware/main/font/ttf_font.c firmware/main/book/book_layout.c \
 firmware/main/folio/format_model.c firmware/main/folio/format_store.c firmware/main/folio/format_transaction.c firmware/main/folio/format_service.c \
 firmware/main/folio/reader_draft.c firmware/main/folio/checkpoint_store.c firmware/main/folio/folio_ui.c firmware/main/folio/format_ui.c \
 firmware/main/apps/app_folio_home.c firmware/main/apps/app_folio_stats.c firmware/main/apps/app_folio_settings.c firmware/main/apps/app_folio_sleep.c firmware/main/apps/app_folio_storage.c \
 firmware/main/apps/app_folio_format.c firmware/main/apps/app_folio_font_picker.c "$out/builtin.S" -lm -lpthread -o "$out/device-test"
"$out/device-test" firmware/sdcard/fonts/ChillDuanSansVF.ttf "$out"
python3 - "$out" <<'PY'
import pathlib,struct,sys,zlib,json,hashlib
out=pathlib.Path(sys.argv[1])
for p in out.glob('*.pgm'):
 _,dims,_,raw=p.read_bytes().split(b'\n',3);w,h=map(int,dims.split())
 def chunk(t,d):return struct.pack('!I',len(d))+t+d+struct.pack('!I',zlib.crc32(t+d)&0xffffffff)
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+raw[y*w:(y+1)*w] for y in range(h))))+chunk(b'IEND',b''))
files=['firmware/main/apps/app_book.c','firmware/main/folio/format_service.c','firmware/main/book/book_layout.c','firmware/main/font/ttf_font.c']
(out/'manifest.json').write_text(json.dumps({'level':'host production rendering; no panel timings','sources':{p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in files}},indent=2))
print(out)
PY
