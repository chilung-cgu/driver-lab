#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)

make -C "$ROOT_DIR/runtime"
test -x "$ROOT_DIR/tests/driver_lab_char_cli"

printf '08-runtime-library basic build test passed.\n'
