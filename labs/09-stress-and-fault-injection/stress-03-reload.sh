#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
SUDO=
i=0

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

make -C "$LAB_DIR"

while [ "$i" -lt 20 ]; do
    if lsmod | grep -q '^driver_lab_ioctl_poll_mmap '; then
        $SUDO rmmod driver_lab_ioctl_poll_mmap
    fi

    $SUDO insmod "$LAB_DIR/driver_lab_ioctl_poll_mmap.ko"
    $SUDO rmmod driver_lab_ioctl_poll_mmap
    i=$((i + 1))
done

printf 'stress-03-reload passed.\n'
