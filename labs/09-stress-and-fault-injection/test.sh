#!/bin/sh
set -eu

# 這支入口目前串起 03 專用 stress 腳本；不是完整 fault-injection framework。
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

"$SCRIPT_DIR/stress-03-reload.sh"
"$SCRIPT_DIR/stress-03-parallel.sh"

printf '09-stress-and-fault-injection basic stress suite passed.\n'
