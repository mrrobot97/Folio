#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 只构建，不刷写；所有工具/缓存留在仓库。/ Build only; keep tools and caches inside the repository.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export IDF_PATH="$root/.tools/esp-idf"
export IDF_TOOLS_PATH="$root/.tools/espressif"
export IDF_COMPONENT_CACHE_PATH="$root/.cache/idf-components"
export XDG_CACHE_HOME="$root/.cache"
export PIP_CACHE_DIR="$root/.cache/pip"
export CCACHE_DIR="$root/.cache/ccache"
# 官方环境脚本仅影响当前子进程。/ Official activation affects this subprocess only.
source "$IDF_PATH/export.sh" >/dev/null
idf.py --version
project=${FOLIO_PROJECT_DIR:-"$root/firmware"}
idf.py -C "$project" "$@" build
