#!/bin/sh
set -eu

# 對 03 driver 做 parallel userspace access。
# 目標是提高 read/write/ioctl/poll 共享狀態被同時碰到的機率。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
MODULE_NAME=driver_lab_ioctl_poll_mmap
SUDO=

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

cleanup() {
	if lsmod | grep -q "^${MODULE_NAME} "; then
		$SUDO rmmod "$MODULE_NAME" || true
	fi
	make -C "$LAB_DIR" clean >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

make -C "$LAB_DIR"
make -C "$ROOT_DIR/runtime"

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_ctl0 \
	/sys/class/driver_lab_ctl/driver_lab_ctl0 \
	driver_lab_ctl

# 每個 worker 反覆呼叫同一個 CLI，模擬多個 userspace client 同時打 driver。
worker() {
	idx=$1
	i=0
	while [ "$i" -lt 20 ]; do
		$SUDO "$CLI" /dev/driver_lab_ctl0 ioctl-write "worker-$idx-$i" >/dev/null
		$SUDO "$CLI" /dev/driver_lab_ctl0 status >/dev/null
		# read 是消費型語意；parallel worker 可能被別的 worker 先讀走資料。
		# 這裡用 timeout 避免 stress script 因預期中的競爭而永久等待。
		timeout 2s $SUDO "$CLI" /dev/driver_lab_ctl0 read >/dev/null || true
		$SUDO "$CLI" /dev/driver_lab_ctl0 trigger >/dev/null
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

$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_ctl0 "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
printf 'stress-03-parallel passed.\n'
