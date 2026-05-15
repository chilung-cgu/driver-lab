#!/bin/sh
set -eu

# 這支測試只驗證 runtime/CLI 能 build，以及 CLI 無參數時會印 usage。
# 真正 driver 行為驗證仍回到 02/03 的 Linux smoke test。
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
OUTPUT_LOG=$(mktemp)

cleanup() {
    rm -f "$OUTPUT_LOG"
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR/runtime"
test -x "$CLI"

if "$CLI" >"$OUTPUT_LOG" 2>&1; then
    printf 'ERROR: CLI without arguments should print usage and exit non-zero.\n' >&2
    exit 1
fi

grep -q 'Usage:' "$OUTPUT_LOG"

printf '08-runtime-library basic build test passed.\n'
