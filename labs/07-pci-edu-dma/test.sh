#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux guest 上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_dma
DMESG_LOG=$(mktemp)
DMESG_ALL=$(mktemp)
DMESG_MARKER="${MODULE_NAME}: smoke-test marker pid=$$ epoch=$(date +%s)"
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

printf '%s\n' "$DMESG_MARKER" | $SUDO tee /dev/kmsg >/dev/null

$SUDO insmod "./${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
fs_expect_proc_interrupt "$MODULE_NAME"

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" \
    "PCI driver sysfs directory"

$SUDO dmesg >"$DMESG_ALL"
if ! grep -Fq "$DMESG_MARKER" "$DMESG_ALL"; then
    printf 'ERROR: kernel log marker was lost; cannot isolate this test run.\n' >&2
    exit 1
fi
awk -v marker="$DMESG_MARKER" '
    index($0, marker) { capture = 1; next }
    capture { print }
' "$DMESG_ALL" >"$DMESG_LOG"
cat "$DMESG_LOG"

grep -q "${MODULE_NAME}:" "$DMESG_LOG"
grep -q 'probe takeover confirmed DMA command idle with BME disabled' \
    "$DMESG_LOG"
grep -Fq 'dma mask configured to 28 bits' "$DMESG_LOG"
grep -q 'coherent buffer allocated' "$DMESG_LOG"
grep -q 'EDU IRQ ACK regression: unknown status=0x80000000 cleared without completion' \
    "$DMESG_LOG"
grep -q 'EDU IRQ ACK regression: all-status=0xffffffff cleared; completion drained' \
    "$DMESG_LOG"
grep -q 'ram-to-edu transfer finished' "$DMESG_LOG"
grep -q 'edu-to-ram transfer finished' "$DMESG_LOG"
grep -q 'round-trip compare passed, irq_count=3 last_status=0x00000100' \
    "$DMESG_LOG"
grep -q 'device removed' "$DMESG_LOG"

if grep -F 'acknowledged unexpected known EDU IRQ status=' "$DMESG_LOG" >/dev/null ||
    grep -F 'acknowledged EDU IRQ with unknown bits status=' "$DMESG_LOG" |
        grep -Ev 'status=0x(80000000|ffffffff)' >/dev/null; then
    printf 'ERROR: uncontrolled unexpected EDU IRQ status in this run.\n' >&2
    exit 1
fi

if grep -Eq 'BUG:|WARNING:|KASAN:|KCSAN:|Oops:|use-after-free|cannot prove DMA quiescence|retaining coherent mapping|coherent allocation intentionally retained' \
    "$DMESG_LOG"; then
    printf 'ERROR: kernel warning/sanitizer/quiesce failure in this test run.\n' >&2
    exit 1
fi

make clean
printf '07-pci-edu-dma smoke test passed.\n'
