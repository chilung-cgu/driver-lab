#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux guest 上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_irq
DMESG_LOG=$(mktemp)
DMESG_ALL=$(mktemp)
SUDO=
loaded_by_test=0

cleanup() {
    if [ "$loaded_by_test" -eq 1 ] && \
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$DMESG_LOG" "$DMESG_ALL"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi
if ! lspci -Dnn | grep -q '1234:11e8'; then
    printf 'ERROR: guest 內看不到 QEMU EDU (1234:11e8)。\n' >&2
    exit 1
fi
fs_expect_pci_device_id 0x1234 0x11e8

if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
make

before_lines=$($SUDO dmesg | wc -l)

$SUDO insmod "./${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
fs_expect_proc_interrupt "$MODULE_NAME"

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" \
    "PCI driver sysfs directory"

$SUDO dmesg >"$DMESG_ALL"
after_lines=$(wc -l <"$DMESG_ALL")
if [ "$after_lines" -lt "$before_lines" ]; then
    printf 'ERROR: kernel log wrapped during the test; cannot isolate new lines.\n' >&2
    exit 1
fi
start_line=$((before_lines + 1))
tail -n "+$start_line" "$DMESG_ALL" >"$DMESG_LOG"
cat "$DMESG_LOG"

grep -q "${MODULE_NAME}: probe start" "$DMESG_LOG"
grep -q 'request_irq ok' "$DMESG_LOG"
grep -q 'irq status=0x' "$DMESG_LOG"
grep -q 'self-test passed count=' "$DMESG_LOG"
grep -q 'device removed' "$DMESG_LOG"

if grep -Eq 'timed out|still set|BUG:|WARNING:|KASAN:|KCSAN:|Oops:|use-after-free' \
    "$DMESG_LOG"; then
    printf 'ERROR: IRQ failure, kernel warning, or sanitizer report in this run.\n' >&2
    exit 1
fi

make clean
printf '06-pci-edu-irq smoke test passed.\n'
