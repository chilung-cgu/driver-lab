#!/bin/sh
set -eu

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
