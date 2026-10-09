#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/system
flags=(-std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
cc "${flags[@]}" -Itests/system/stubs -Ifirmware/tools/book_storage_stubs -Ifirmware/main \
    tests/system/settings_safety_test.c firmware/main/settings.c -o build/system/settings
build/system/settings
cc "${flags[@]}" -Ifirmware/main/book -Ifirmware/tools/book_storage_stubs \
    -Ifirmware/components/read_pico/include firmware/tools/book_store_host_test.c -o build/system/store
build/system/store
cc "${flags[@]}" -Itests/system/stubs -Ifirmware/main/folio \
    tests/system/clock_test.c firmware/main/folio/folio_clock.c -o build/system/clock
build/system/clock
python3 - <<'PY'
from pathlib import Path
import struct,zlib
pixels=bytes(range(256))*(684*1216//2//256)+bytes(range((684*1216//2)%256))
Path('build/system/wallpaper.fwp').write_bytes(struct.pack('<4sHHII',b'FWP1',684,1216,len(pixels),zlib.crc32(pixels))+pixels)
PY
cc "${flags[@]}" -Ifirmware/main/folio tests/system/wallpaper_test.c firmware/main/folio/wallpaper.c -o build/system/wallpaper
build/system/wallpaper build/system/wallpaper.fwp
