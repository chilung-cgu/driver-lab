#!/bin/sh
set -eu

# 單一 lab 的 quality.sh 只是便利入口。
# 真正的檢查邏輯集中在 repo 根目錄 scripts/quality.sh，避免每個 lab 複製一份。
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
exec "$ROOT_DIR/scripts/quality.sh" "$SCRIPT_DIR"
