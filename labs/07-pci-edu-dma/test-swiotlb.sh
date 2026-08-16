#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test-swiotlb.sh 必須在 Linux guest 上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_dma
DMA_ADDRESS_BITS=${EDU_DMA_ADDRESS_BITS:-28}
DMESG_LOG=$(mktemp)
DMESG_ALL=$(mktemp)
TRACE_LOG=$(mktemp)
TRACE_WINDOW=$(mktemp)
DMESG_MARKER="${MODULE_NAME}: swiotlb-test marker pid=$$ epoch=$(date +%s)"
TRACE_BEGIN="driver_lab_swiotlb_begin_$$"
TRACE_END="driver_lab_swiotlb_end_$$"
SUDO=
loaded_by_test=0
trace_configured=0
trace_event_was_enabled=
tracing_was_on=

cleanup() {
    if [ "$loaded_by_test" -eq 1 ] && \
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    if [ "$trace_configured" -eq 1 ]; then
        printf '%s\n' "$trace_event_was_enabled" |
            $SUDO tee "$TRACE_ENABLE" >/dev/null || true
        printf '%s\n' "$tracing_was_on" |
            $SUDO tee "$TRACING_ON" >/dev/null || true
    fi
    rm -f "$DMESG_LOG" "$DMESG_ALL" "$TRACE_LOG" "$TRACE_WINDOW"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

case "$DMA_ADDRESS_BITS" in
    28|32)
        ;;
    *)
        printf 'ERROR: EDU_DMA_ADDRESS_BITS must be 28 (default EDU) or 32 (dedicated fixture).\n' >&2
        exit 1
        ;;
esac

if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi
EDU_BDFS=$(lspci -Dnn | awk '/1234:11e8/ { print $1 }')
EDU_COUNT=$(printf '%s\n' "$EDU_BDFS" | awk 'NF { count++ } END { print count + 0 }')
if [ "$EDU_COUNT" -ne 1 ]; then
    printf 'ERROR: SWIOTLB fixture must expose exactly one QEMU EDU (1234:11e8); found %s.\n' \
        "$EDU_COUNT" >&2
    exit 1
fi
EDU_BDF=$EDU_BDFS
fs_expect_pci_device_id 0x1234 0x11e8

if [ "$DMA_ADDRESS_BITS" -ne 28 ]; then
    printf '%s\n' \
        "INFO: non-default EDU aperture requires matching host configuration; record -device edu,dma_mask before treating this as evidence."
fi

if ! tr ' ' '\n' </proc/cmdline | grep -Fxq 'swiotlb=force'; then
    printf 'ERROR: 此測試只接受 boot 參數 swiotlb=force 的獨立 guest。\n' >&2
    exit 1
fi
if [ -d /sys/kernel/iommu_groups ]; then
    iommu_group_device=$(find /sys/kernel/iommu_groups -type l -print -quit \
        2>/dev/null || true)
    if [ -n "$iommu_group_device" ]; then
        printf 'ERROR: 偵測到 IOMMU group (%s)；請使用無 IOMMU 的 SWIOTLB guest。\n' \
            "$iommu_group_device" >&2
        exit 1
    fi
fi
if ! $SUDO dmesg | grep -Fq 'PCI-DMA: Using software bounce buffering for IO (SWIOTLB)'; then
    printf 'ERROR: boot log 未確認 SWIOTLB software bounce buffering。\n' >&2
    exit 1
fi

TRACEFS=
for candidate in /sys/kernel/tracing /sys/kernel/debug/tracing; do
    if $SUDO test -d "$candidate"; then
        TRACEFS=$candidate
        break
    fi
done
if [ -z "$TRACEFS" ]; then
    printf 'ERROR: 找不到已掛載的 tracefs。\n' >&2
    exit 1
fi
TRACE_ENABLE="$TRACEFS/events/swiotlb/swiotlb_bounced/enable"
TRACE_MARKER="$TRACEFS/trace_marker"
TRACE_DATA="$TRACEFS/trace"
TRACING_ON="$TRACEFS/tracing_on"
if ! $SUDO test -e "$TRACE_ENABLE" || ! $SUDO test -e "$TRACE_MARKER" ||
   ! $SUDO test -e "$TRACE_DATA" || ! $SUDO test -e "$TRACING_ON"; then
    printf 'ERROR: 這個 kernel 未提供完整的 swiotlb_bounced trace event。\n' >&2
    exit 1
fi

if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

trace_event_was_enabled=$($SUDO cat "$TRACE_ENABLE")
tracing_was_on=$($SUDO cat "$TRACING_ON")
printf '%s\n' 1 | $SUDO tee "$TRACE_ENABLE" >/dev/null
if [ "$tracing_was_on" = 0 ]; then
    printf '%s\n' 1 | $SUDO tee "$TRACING_ON" >/dev/null
fi
trace_configured=1

cd "$SCRIPT_DIR"
make

printf '%s\n' "$DMESG_MARKER" | $SUDO tee /dev/kmsg >/dev/null
printf '%s\n' "$TRACE_BEGIN" | $SUDO tee "$TRACE_MARKER" >/dev/null

$SUDO insmod "./${MODULE_NAME}.ko" streaming_probe=1 \
    dma_address_bits="$DMA_ADDRESS_BITS"
loaded_by_test=1
actual_dma_address_bits=$($SUDO cat "/sys/module/${MODULE_NAME}/parameters/dma_address_bits")
if [ "$actual_dma_address_bits" != "$DMA_ADDRESS_BITS" ]; then
    printf 'ERROR: module parameter mismatch: expected %s, got %s.\n' \
        "$DMA_ADDRESS_BITS" "$actual_dma_address_bits" >&2
    exit 1
fi
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
fs_expect_proc_interrupt "$MODULE_NAME"

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" \
    "PCI driver sysfs directory"

printf '%s\n' "$TRACE_END" | $SUDO tee "$TRACE_MARKER" >/dev/null
$SUDO cat "$TRACE_DATA" >"$TRACE_LOG"
awk -v begin="$TRACE_BEGIN" -v end="$TRACE_END" '
    index($0, begin) { capture = 1; next }
    index($0, end) { exit }
    capture { print }
' "$TRACE_LOG" >"$TRACE_WINDOW"

printf '%s\n' "$trace_event_was_enabled" |
    $SUDO tee "$TRACE_ENABLE" >/dev/null
printf '%s\n' "$tracing_was_on" | $SUDO tee "$TRACING_ON" >/dev/null
trace_configured=0

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

PAGE_BYTES=$(getconf PAGESIZE)
EXPECTED_DMA_MASK=fffffff
if [ "$DMA_ADDRESS_BITS" -eq 32 ]; then
    EXPECTED_DMA_MASK=ffffffff
fi
if ! grep -F "dev_name: $EDU_BDF " "$TRACE_WINDOW" >"$TRACE_LOG"; then
    printf 'ERROR: 沒有觀察到 %s 的 SWIOTLB trace event。\n' "$EDU_BDF" >&2
    cat "$TRACE_WINDOW" >&2
    exit 1
fi
if ! grep -F "dma_mask=$EXPECTED_DMA_MASK" "$TRACE_LOG" |
    grep -Fq "size=$PAGE_BYTES FORCE"; then
    printf 'ERROR: 沒有觀察到 %s 的 %s-byte FORCE SWIOTLB bounce trace。\n' \
        "$EDU_BDF" "$PAGE_BYTES" >&2
    cat "$TRACE_LOG" >&2
    exit 1
fi
if [ "$DMA_ADDRESS_BITS" -eq 32 ]; then
    if ! sed -n 's/.*dev_addr=\([0-9a-f][0-9a-f]*\) .*/\1/p' "$TRACE_LOG" |
        awk 'BEGIN { ok = 0; limit = 268435455 }
             { val = 0
               for (i = 1; i <= length($0); i++) {
                   c = substr($0, i, 1)
                   n = index("0123456789abcdef", c) - 1
                   if (n < 0) { val = -1; break }
                   val = val * 16 + n
               }
               if (val > limit) ok = 1 }
             END { exit !ok }'; then
        printf '%s\n' \
            'ERROR: 32-bit fixture did not observe a streaming DMA address above the default 28-bit aperture.' >&2
        cat "$TRACE_LOG" >&2
        exit 1
    fi
fi
cat "$TRACE_LOG"

grep -q "${MODULE_NAME}:" "$DMESG_LOG"
grep -q 'probe takeover confirmed DMA command idle with BME disabled' \
    "$DMESG_LOG"
grep -Fq "dma mask configured to ${DMA_ADDRESS_BITS} bits" "$DMESG_LOG"
grep -q 'coherent buffer allocated' "$DMESG_LOG"
grep -q 'EDU IRQ ACK regression: unknown status=0x80000000 cleared without completion' \
    "$DMESG_LOG"
grep -q 'EDU IRQ ACK regression: all-status=0xffffffff cleared; completion drained' \
    "$DMESG_LOG"
grep -q 'round-trip compare passed, irq_count=3 last_status=0x00000100' \
    "$DMESG_LOG"
grep -q 'streaming TX map established:' "$DMESG_LOG"
grep -q 'streaming ram-to-edu transfer finished' "$DMESG_LOG"
grep -q 'streaming TX mapping released after transfer' "$DMESG_LOG"
grep -q 'edu-to-coherent-rx transfer finished' "$DMESG_LOG"
grep -q 'streaming-to-EDU-to-coherent-RX compare passed, irq_count=5 last_status=0x00000100' \
    "$DMESG_LOG"
grep -q 'device removed' "$DMESG_LOG"

if grep -F 'acknowledged unexpected known EDU IRQ status=' "$DMESG_LOG" >/dev/null ||
    grep -F 'acknowledged EDU IRQ with unknown bits status=' "$DMESG_LOG" |
        grep -Ev 'status=0x(80000000|ffffffff)' >/dev/null; then
    printf 'ERROR: uncontrolled unexpected EDU IRQ status in this run.\n' >&2
    exit 1
fi

if grep -Eq 'BUG:|WARNING:|KASAN:|KCSAN:|Oops:|use-after-free|DMA-API:|cannot prove DMA quiescence|retaining coherent mapping|coherent allocation intentionally retained|streaming mapping intentionally retained' \
    "$DMESG_LOG"; then
    printf 'ERROR: kernel warning/sanitizer/DMA/quiesce failure in this test run.\n' >&2
    exit 1
fi

make clean
printf '07-pci-edu-dma forced-SWIOTLB streaming smoke test passed (dma_address_bits=%s).\n' \
    "$DMA_ADDRESS_BITS"
