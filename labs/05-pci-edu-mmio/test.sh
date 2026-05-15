#!/bin/sh
set -eu

# 這支 smoke test 必須在看得到 QEMU EDU 的 Linux guest 內跑。
# 它驗證 PCI probe、BAR0 map、liveness MMIO read/write。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機或 Linux guest 上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
MODULE_NAME=driver_lab_edu_mmio
DMESG_LOG=$(mktemp)
SUDO=

cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
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

# 如果前一次測試留下同名 module，先卸載，避免 bind 狀態混亂。
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

# 清 dmesg 只是為了讓本次測試的成功訊號比較容易 grep。
$SUDO dmesg -C || true
$SUDO insmod "./${MODULE_NAME}.ko"
$SUDO dmesg | tee "$DMESG_LOG"

grep -q 'probe start' "$DMESG_LOG"
grep -q 'BAR0 mapped' "$DMESG_LOG"
grep -q 'liveness check passed' "$DMESG_LOG"

$SUDO rmmod "$MODULE_NAME"
make clean

printf '05-pci-edu-mmio smoke test passed.\n'
