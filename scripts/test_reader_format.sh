#!/bin/sh
# 真实模型与检查点文件层故障测试，不接触设备。/ Real model/file fault tests, without device access.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d /tmp/folio-format-build.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags='-std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
"${CC:-cc}" $flags -I "$task_root/firmware/main/folio" -Dfopen=folio_fault_fopen -Dfwrite=folio_fault_fwrite -Dfflush=folio_fault_fflush -Dfclose=folio_fault_fclose -Dfsync=folio_fault_fsync -c "$task_root/firmware/main/folio/checkpoint_store.c" -o "$build/checkpoint.o"
"${CC:-cc}" $flags -I "$task_root/firmware/main/folio" -Dcalloc=folio_format_fault_calloc -c "$task_root/firmware/main/folio/format_store.c" -o "$build/store.o"
"${CC:-cc}" $flags -I "$task_root/firmware/main/folio" -Dcalloc=folio_format_fault_calloc -c "$task_root/firmware/main/folio/format_transaction.c" -o "$build/transaction.o"
"${CC:-cc}" $flags -I "$task_root/firmware/main/folio" "$task_root/firmware/main/folio/format_model.c" "$task_root/tests/reader/test_format.c" "$build/checkpoint.o" "$build/store.o" "$build/transaction.o" -o "$build/test"
"$build/test"
