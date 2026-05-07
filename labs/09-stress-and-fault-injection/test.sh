#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

"$SCRIPT_DIR/stress-03-reload.sh"
"$SCRIPT_DIR/stress-03-parallel.sh"

printf '09-stress-and-fault-injection basic stress suite passed.\n'
