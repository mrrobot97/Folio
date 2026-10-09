#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="${1:-/tmp/folio-format-ui-host}"
mkdir -p "$out"
python3 - "$out" <<'PY'
import pathlib,platform,sys
out=pathlib.Path(sys.argv[1]);font=pathlib.Path('firmware/main/assets/builtin.ttf').resolve()
section='.section __TEXT,__const' if platform.system()=='Darwin' else '.section .rodata'
(out/'builtin.S').write_text(f'{section}\n.globl _binary_builtin_ttf_start\n.globl _binary_builtin_ttf_end\n_binary_builtin_ttf_start:\n.incbin "{font}"\n_binary_builtin_ttf_end:\n')
PY
cc -std=gnu11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g \
  -Itests/ui/format_stubs -Itests/ui/stubs -Itests/fonts/stubs -Ifirmware/main/font -Ifirmware/main \
  -Dttf_font_scan=engine_font_scan -Dttf_font_count=engine_font_count -Dttf_font_item=engine_font_item \
  -c firmware/main/font/ttf_font.c -o "$out/ttf_font.o"
cc -std=gnu11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g \
  -Itests/ui/format_stubs -Itests/ui/stubs -Itests/fonts/stubs -Ifirmware/main/app -Ifirmware/main/ui -Ifirmware/main/font -Ifirmware/main/folio -Ifirmware/main/book -Ifirmware/main \
  tests/ui/format_host_test.c firmware/main/folio/folio_ui.c firmware/main/folio/format_ui.c firmware/main/folio/format_model.c \
  firmware/main/apps/app_folio_home.c firmware/main/apps/app_folio_stats.c firmware/main/apps/app_folio_settings.c firmware/main/apps/app_folio_sleep.c firmware/main/apps/app_folio_storage.c \
  firmware/main/apps/app_folio_format.c firmware/main/apps/app_folio_font_picker.c "$out/ttf_font.o" "$out/builtin.S" -lm -lpthread -o "$out/format-ui-test"
design_font=/Users/catfish/read_pico_firmware/docs/local/ui-redesign/v0.4.2/output/fonts/PicoDesignSans-Regular.ttf
if [ -f "$design_font" ]; then
  "$out/format-ui-test" firmware/sdcard/fonts/ChillDuanSansVF.ttf "$out" "$design_font"
else
  "$out/format-ui-test" firmware/sdcard/fonts/ChillDuanSansVF.ttf "$out"
fi
python3 - "$out" <<'PY'
import datetime,hashlib,json,pathlib,struct,sys,zlib
out=pathlib.Path(sys.argv[1])
font=pathlib.Path('firmware/main/assets/builtin.ttf')
sources=[pathlib.Path(p) for p in ['firmware/main/apps/app_folio_format.c','firmware/main/apps/app_folio_font_picker.c','firmware/main/folio/format_ui.c','firmware/main/font/ttf_font.c']]
manifest={'generated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'font_bytes':font.stat().st_size,'font_sha256':hashlib.sha256(font.read_bytes()).hexdigest(),'production_sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
(out/'render-manifest.json').write_text(json.dumps(manifest,indent=2))
for p in out.glob('*.pgm'):
    magic,dims,depth,raw=p.read_bytes().split(b'\n',3);w,h=map(int,dims.split())
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+raw[y*w:(y+1)*w] for y in range(h))
    p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
PY
