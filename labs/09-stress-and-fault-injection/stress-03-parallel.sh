#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
SUDO=

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

make -C "$LAB_DIR"
make -C "$ROOT_DIR/runtime"

if lsmod | grep -q '^driver_lab_ioctl_poll_mmap '; then
    $SUDO rmmod driver_lab_ioctl_poll_mmap
fi

$SUDO insmod "$LAB_DIR/driver_lab_ioctl_poll_mmap.ko"

worker() {
    idx=$1
    i=0
    while [ "$i" -lt 20 ]; do
        "$CLI" /dev/driver_lab_ctl0 ioctl-write "worker-$idx-$i" >/dev/null
        "$CLI" /dev/driver_lab_ctl0 status >/dev/null
        "$CLI" /dev/driver_lab_ctl0 read >/dev/null
        "$CLI" /dev/driver_lab_ctl0 trigger >/dev/null
        i=$((i + 1))
    done
}

worker 0 &
pid0=$!
worker 1 &
pid1=$!
worker 2 &
pid2=$!
worker 3 &
pid3=$!

wait "$pid0" "$pid1" "$pid2" "$pid3"

$SUDO rmmod driver_lab_ioctl_poll_mmap
printf 'stress-03-parallel passed.\n'
