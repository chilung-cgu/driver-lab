#!/bin/sh
set -eu

# 對 03 driver 做 repeated load/unload。
# 目標是檢查 init/exit cleanup 是否對稱，避免多跑幾次才爆的問題。
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

# 連續 load/unload 20 次；若 cleanup 不完整，這類測試通常比單次 smoke test 更容易暴露。
while [ "$i" -lt 20 ]; do
    if lsmod | grep -q '^driver_lab_ioctl_poll_mmap '; then
        $SUDO rmmod driver_lab_ioctl_poll_mmap
    fi

    $SUDO insmod "$LAB_DIR/driver_lab_ioctl_poll_mmap.ko"
    $SUDO rmmod driver_lab_ioctl_poll_mmap
    i=$((i + 1))
done

printf 'stress-03-reload passed.\n'
