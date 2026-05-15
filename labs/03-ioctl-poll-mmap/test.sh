#!/bin/sh
set -eu

# 這支 smoke test 驗證 03 的四條路徑：ioctl、read、mmap、poll。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
SUDO=
POLL_LOG=$(mktemp)

cleanup() {
    rm -f "$POLL_LOG"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

cd "$SCRIPT_DIR"
make
make -C "$ROOT_DIR/runtime"

# 如果前一次測試留下同名 module，先卸載，避免 device node 狀態混亂。
if lsmod | grep -q '^driver_lab_ioctl_poll_mmap '; then
    $SUDO rmmod driver_lab_ioctl_poll_mmap
fi

$SUDO insmod ./driver_lab_ioctl_poll_mmap.ko

# CLI 透過 runtime 呼叫 driver，這裡逐一驗證 control/data/shared/event path。
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 ioctl-write hello-ioctl
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 status | grep 'buffer_len='
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 read | grep 'hello-ioctl'
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 mmap-read | grep 'magic=0x'

# 背景 poll 先等事件；主流程再 trigger，確認 waitqueue path 真的會喚醒。
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 poll 3000 >"$POLL_LOG" &
poll_pid=$!
sleep 1
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 trigger
wait "$poll_pid"
grep 'poll ret=1' "$POLL_LOG"

"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 clear
$SUDO rmmod driver_lab_ioctl_poll_mmap
make clean

printf '03-ioctl-poll-mmap smoke test passed.\n'
