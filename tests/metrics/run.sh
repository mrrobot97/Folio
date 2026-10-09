#!/bin/sh
# 主机夹具只写临时目录，不连接设备。/ Host fixtures use temporary files and never connect to devices.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build=$(mktemp -d /tmp/folio-metrics.XXXXXX)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -DCONFIG_FOLIO_METRICS=1 \
    -I "$task_root/tests/metrics/stubs" -I "$task_root/firmware/main/folio" \
    "$task_root/firmware/main/folio/folio_metrics.c" "$task_root/tests/metrics/test_metrics.c" \
    -o "$test_build/test_metrics"
"$test_build/test_metrics" > "$test_build/host-fixture.log"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O0 \
    -I "$task_root/firmware/main/folio" \
    "$task_root/firmware/main/folio/folio_metrics.c" "$task_root/tests/metrics/test_disabled.c" \
    -o "$test_build/test_disabled"
"$test_build/test_disabled"
python3 "$task_root/tests/metrics/test_analyzer.py" "$test_build/host-fixture.log"
