#!/bin/sh
set -eu

# Unsafe mode is an intentionally racy demonstration. Safe mode has a hard
# correctness gate: every successful userspace increment must be reflected.
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CLI=$(mktemp)
RAW_IOCTL=$(mktemp)
MODULE_NAME=driver_lab_race
DEVICE=/dev/driver_lab_race0
SUDO=
UNSAFE_LOG=$(mktemp)
SAFE_LOG=$(mktemp)
loaded_by_test=0

cleanup() {
    if [ "$loaded_by_test" -eq 1 ] &&
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$UNSAFE_LOG" "$SAFE_LOG" "$CLI" "$RAW_IOCTL"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s 已在載入；test不會卸載非本次載入的module。\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
make
cc -Wall -Wextra -Werror -std=c11 -pthread -o "$CLI" \
    "$ROOT_DIR/tests/driver_lab_race_cli.c"
cc -Wall -Wextra -Werror -std=c11 -o "$RAW_IOCTL" \
    "$ROOT_DIR/tests/driver_lab_race_raw_ioctl.c"

$SUDO insmod "./${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_char_device "$DEVICE" \
    /sys/class/driver_lab_race/driver_lab_race0 \
    driver_lab_race

$SUDO "$CLI" "$DEVICE" status | grep 'worker_running=1'

# Unsafe phase: mode/reset are quiescent boundaries, while increments within
# the phase may race. A visible lost update is useful but timing-dependent.
$SUDO "$CLI" "$DEVICE" safe-mode 0
$SUDO "$CLI" "$DEVICE" reset
$SUDO "$CLI" "$DEVICE" race 8 50 | tee "$UNSAFE_LOG"

grep 'safe_mode=0' "$UNSAFE_LOG"
unsafe_expected=$(sed -n \
    's/.*expected_at_least=\([0-9][0-9]*\).*/\1/p' "$UNSAFE_LOG")
unsafe_observed=$(sed -n \
    's/.*observed=\([0-9][0-9]*\).*/\1/p' "$UNSAFE_LOG")
[ -n "$unsafe_expected" ]
[ -n "$unsafe_observed" ]
if [ "$unsafe_observed" -ge "$unsafe_expected" ]; then
    printf '%s\n' \
        'NOTE: this unsafe run did not expose a net lost-update deficit; it remains intentionally racy and needs stress/KCSAN for stronger evidence.' \
        >&2
fi

# Safe phase: switching mode and reset wait for old unsafe increments to exit.
# Every successful userspace ioctl must be counted; the background worker can
# only make observed larger than the minimum.
$SUDO "$CLI" "$DEVICE" safe-mode 1
$SUDO "$CLI" "$DEVICE" reset
$SUDO "$CLI" "$DEVICE" race 8 50 | tee "$SAFE_LOG"

grep 'safe_mode=1' "$SAFE_LOG"
safe_expected=$(sed -n \
    's/.*expected_at_least=\([0-9][0-9]*\).*/\1/p' "$SAFE_LOG")
safe_observed=$(sed -n \
    's/.*observed=\([0-9][0-9]*\).*/\1/p' "$SAFE_LOG")
[ -n "$safe_expected" ]
[ -n "$safe_observed" ]

if [ "$safe_observed" -lt "$safe_expected" ]; then
    printf 'ERROR: safe mode lost successful userspace increments: expected>=%s observed=%s\n' \
        "$safe_expected" "$safe_observed" >&2
    exit 1
fi

# The raw probe bypasses the CLI's 0|1 parser and verifies kernel-side EINVAL.
$SUDO "$RAW_IOCTL" "$DEVICE"

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "$DEVICE" "device node"
fs_expect_absent /sys/class/driver_lab_race/driver_lab_race0 \
    "sysfs class device"
make clean

printf '04-locking-and-races smoke test passed.\n'
