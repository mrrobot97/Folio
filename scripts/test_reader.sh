#!/bin/sh
# 宿主测试不接触设备或正式数据。/ Host tests never touch devices or product data.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build=$(mktemp -d /tmp/folio-reader-build.XXXXXX)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$task_root/firmware/main/folio" \
  "$task_root/firmware/main/folio/checkpoint_store.c" \
  "$task_root/firmware/main/folio/reading_service.c" \
  "$task_root/firmware/main/folio/reader_draft.c" \
  "$task_root/tests/reader/test_reading.c" -o "$test_build/test_reading"
"$test_build/test_reading"
