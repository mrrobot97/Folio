#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="${1:-/tmp/folio-power-host}"
mkdir -p "$out"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itests/power/stubs -Itests/ui/stubs -Ifirmware/main/folio -Ifirmware/main/app -Ifirmware/main/ui -Ifirmware/main \
  tests/power/power_host_test.c firmware/main/folio/folio_power.c firmware/main/sleep.c -o "$out/power-host-test"
"$out/power-host-test"
