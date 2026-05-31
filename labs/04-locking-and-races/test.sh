#!/bin/sh
set -eu

# 這支 smoke test 驗證 race lab：先跑 unsafe，再跑 safe-mode，觀察 mutex 是否改善結果。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CLI="$ROOT_DIR/tests/driver_lab_race_cli"
MODULE_NAME=driver_lab_race
SUDO=
UNSAFE_LOG=$(mktemp)
SAFE_LOG=$(mktemp)

cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$UNSAFE_LOG" "$SAFE_LOG" "$CLI"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

cd "$SCRIPT_DIR"
make
# 這支 CLI 只是在 userspace 端重現 race，不需要另外裝進系統。
cc -Wall -Wextra -Werror -pthread -o "$CLI" "$ROOT_DIR/tests/driver_lab_race_cli.c"

# 如果前一次測試留下同名 module，先卸載，避免背景 worker 狀態混亂。
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_race0 \
	/sys/class/driver_lab_race/driver_lab_race0 \
	driver_lab_race

$SUDO "$CLI" /dev/driver_lab_race0 safe-mode 0
$SUDO "$CLI" /dev/driver_lab_race0 reset
# 先跑故意不加鎖的版本，通常會看到更明顯的 lost update。
$SUDO "$CLI" /dev/driver_lab_race0 race 8 50 | tee "$UNSAFE_LOG"

$SUDO "$CLI" /dev/driver_lab_race0 safe-mode 1
$SUDO "$CLI" /dev/driver_lab_race0 reset
# 再跑修正後版本，用來跟 unsafe 模式做對照。
$SUDO "$CLI" /dev/driver_lab_race0 race 8 50 | tee "$SAFE_LOG"

unsafe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$UNSAFE_LOG")
safe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$SAFE_LOG")

[ -n "$unsafe_observed" ]
[ -n "$safe_observed" ]

if [ "$safe_observed" -lt "$unsafe_observed" ]; then
    printf 'ERROR: safe mode should not perform worse than unsafe mode.\n' >&2
    exit 1
fi

$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_race0 "device node"
fs_expect_absent /sys/class/driver_lab_race/driver_lab_race0 "sysfs class device"
make clean

printf '04-locking-and-races smoke test passed.\n'
