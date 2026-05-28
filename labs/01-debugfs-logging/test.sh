#!/bin/sh
set -eu

# 這支 smoke test 驗證 debugfs 路徑：建立 debugfs 檔案、寫 trigger、觀察 status/log。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
MODULE_NAME=driver_lab_debugfs_logging
SUDO=

cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

"$ROOT_DIR/scripts/mount-debugfs.sh"
cd "$SCRIPT_DIR"
make

# 如果前一次測試留下同名 module，先卸載，避免 insmod 失敗。
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

# 載入後先讀 status，再寫 trigger，確認 driver state 有變化。
$SUDO insmod ./driver_lab_debugfs_logging.ko
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/status
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/trigger
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/trigger_count
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/emit_debug
fs_note_optional_path /proc/dynamic_debug/control "dynamic debug control"
$SUDO cat /sys/kernel/debug/driver_lab_debugfs/status
printf '%s' 'smoke-one' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
$SUDO cat /sys/kernel/debug/driver_lab_debugfs/trigger_count

if [ -e /proc/dynamic_debug/control ]; then
	echo 'module driver_lab_debugfs_logging +p' | $SUDO tee /proc/dynamic_debug/control >/dev/null
    printf '%s' 'smoke-two' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
fi

$SUDO dmesg | tail -n 50 | grep 'driver_lab_debugfs_logging'
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /sys/kernel/debug/driver_lab_debugfs "debugfs directory"
make clean

printf '01-debugfs-logging smoke test passed.\n'
