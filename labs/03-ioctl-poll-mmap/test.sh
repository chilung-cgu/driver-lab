#!/bin/sh
set -eu

# 這支 smoke test 驗證 03 的四條路徑：ioctl、read、mmap、poll。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_ioctl_poll_mmap
SUDO=
POLL_LOG=$(mktemp)
poll_pid=

cleanup() {
    if [ -n "$poll_pid" ] && kill -0 "$poll_pid" 2>/dev/null; then
        kill "$poll_pid" 2>/dev/null || true
        wait "$poll_pid" 2>/dev/null || true
    fi
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$POLL_LOG"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

cd "$SCRIPT_DIR"
make
make -C "$ROOT_DIR/runtime"

# 如果前一次測試留下同名 module，先卸載，避免 device node 狀態混亂。
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_ctl0 \
	/sys/class/driver_lab_ctl/driver_lab_ctl0 \
	driver_lab_ctl

# CLI 透過 runtime 呼叫 driver，這裡逐一驗證 control/data/shared/event path。
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 ioctl-write hello-ioctl
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 status | grep 'buffer_len='
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 read | grep 'hello-ioctl'
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 mmap-read | grep 'magic=0x'

# 背景 poll 先等事件；主流程再 trigger，確認 waitqueue path 真的會喚醒。
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 poll 3000 >"$POLL_LOG" &
poll_pid=$!
sleep 1
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 trigger
wait "$poll_pid"
poll_pid=
grep 'poll ret=1' "$POLL_LOG"

$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 clear
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_ctl0 "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
make clean

printf '03-ioctl-poll-mmap smoke test passed.\n'
