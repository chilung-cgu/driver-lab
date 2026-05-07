#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CLI="$ROOT_DIR/tests/driver_lab_race_cli"
SUDO=
UNSAFE_LOG=$(mktemp)
SAFE_LOG=$(mktemp)

cleanup() {
    rm -f "$UNSAFE_LOG" "$SAFE_LOG" "$CLI"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

cd "$SCRIPT_DIR"
make
# 這支 CLI 只是在 userspace 端重現 race，不需要另外裝進系統。
cc -Wall -Wextra -Werror -pthread -o "$CLI" "$ROOT_DIR/tests/driver_lab_race_cli.c"

if lsmod | grep -q '^driver_lab_race '; then
    $SUDO rmmod driver_lab_race
fi

$SUDO insmod ./driver_lab_race.ko

"$CLI" /dev/driver_lab_race0 safe-mode 0
"$CLI" /dev/driver_lab_race0 reset
# 先跑故意不加鎖的版本，通常會看到更明顯的 lost update。
"$CLI" /dev/driver_lab_race0 race 8 50 | tee "$UNSAFE_LOG"

"$CLI" /dev/driver_lab_race0 safe-mode 1
"$CLI" /dev/driver_lab_race0 reset
# 再跑修正後版本，用來跟 unsafe 模式做對照。
"$CLI" /dev/driver_lab_race0 race 8 50 | tee "$SAFE_LOG"

unsafe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$UNSAFE_LOG")
safe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$SAFE_LOG")

[ -n "$unsafe_observed" ]
[ -n "$safe_observed" ]

if [ "$safe_observed" -lt "$unsafe_observed" ]; then
    printf 'ERROR: safe mode should not perform worse than unsafe mode.\n' >&2
    exit 1
fi

$SUDO rmmod driver_lab_race
make clean

printf '04-locking-and-races smoke test passed.\n'
