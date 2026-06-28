#!/bin/sh
set -eu

# 08 的檢查範圍是 runtime/，因為這一關不是 kernel module lab。
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
exec "$ROOT_DIR/scripts/quality.sh" "$ROOT_DIR/runtime"
