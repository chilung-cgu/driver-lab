#!/bin/sh
set -eu

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

if lsmod | grep -q '^driver_lab_ioctl_poll_mmap '; then
    $SUDO rmmod driver_lab_ioctl_poll_mmap
fi

$SUDO insmod ./driver_lab_ioctl_poll_mmap.ko

"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 ioctl-write hello-ioctl
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 status | grep 'buffer_len='
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 read | grep 'hello-ioctl'
"$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 mmap-read | grep 'magic=0x'

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
