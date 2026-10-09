#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 只在仓库内准备固定版本工具链，不改 shell 配置。/ Install only inside this repo; never edit shell profiles.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export IDF_PATH="$root/.tools/esp-idf"
export IDF_TOOLS_PATH="$root/.tools/espressif"
export PIP_CACHE_DIR="$root/.cache/pip"
expected=fff9895c82d744c7237be8847347bdd1b07c6643
if [[ ! -d "$IDF_PATH/.git" ]]; then
    git clone --depth 1 --branch v6.1 https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi
actual=$(git -C "$IDF_PATH" rev-parse HEAD)
if [[ "$actual" != "$expected" ]]; then
    echo "Unexpected ESP-IDF revision: $actual; expected $expected" >&2
    exit 1
fi
git -C "$IDF_PATH" submodule update --init --recursive --depth 1
bash "$IDF_PATH/install.sh" esp32s3
