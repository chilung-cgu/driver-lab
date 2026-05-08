#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機或 Linux guest 上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
MODULE_NAME=driver_lab_edu_irq
DMESG_LOG=$(mktemp)
SUDO=

cleanup() {
    rm -f "$DMESG_LOG"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi

if ! lspci -nn | grep -q '1234:11e8'; then
    printf 'ERROR: guest 內看不到 QEMU edu (1234:11e8)。\n' >&2
    exit 1
fi

cd "$SCRIPT_DIR"
make

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO dmesg -C || true
$SUDO insmod "./${MODULE_NAME}.ko"
$SUDO dmesg | tee "$DMESG_LOG"

grep -q 'request_irq ok' "$DMESG_LOG"
grep -q 'irq status=' "$DMESG_LOG"
grep -q 'irq self-test passed' "$DMESG_LOG"

$SUDO rmmod "$MODULE_NAME"
make clean

printf '06-pci-edu-irq smoke test passed.\n'
