#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR"

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
SUDO=

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

"$ROOT_DIR/scripts/mount-debugfs.sh"
make

if lsmod | grep -q '^driver_lab_debugfs_logging '; then
    $SUDO rmmod driver_lab_debugfs_logging
fi

$SUDO insmod ./driver_lab_debugfs_logging.ko
cat /sys/kernel/debug/driver_lab_debugfs/status
printf '%s' 'smoke-one' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
cat /sys/kernel/debug/driver_lab_debugfs/trigger_count

if [ -e /proc/dynamic_debug/control ]; then
    echo 'module driver_lab_debugfs_logging +p' | $SUDO tee /proc/dynamic_debug/control >/dev/null
    printf '%s' 'smoke-two' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
fi

$SUDO dmesg | tail -n 50 | grep 'driver_lab_debugfs_logging'
$SUDO rmmod driver_lab_debugfs_logging
make clean

printf '01-debugfs-logging smoke test passed.\n'
