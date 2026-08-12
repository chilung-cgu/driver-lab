#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_ioctl_poll_mmap
DEVICE=/dev/driver_lab_ctl0
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
SUDO=
POLL_LOG=$(mktemp)
EMPTY_POLL_LOG=$(mktemp)
READER1_LOG=$(mktemp)
READER2_LOG=$(mktemp)
poll_pid=
reader1_pid=
reader2_pid=
loaded_by_test=0

stop_pid() {
    pid=$1
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

count_matches() {
    pattern=$1
    shift
    total=0

    for file do
        count=$(grep -c "$pattern" "$file" || true)
        total=$((total + count))
    done
    printf '%s\n' "$total"
}

cleanup() {
    stop_pid "$poll_pid"
    stop_pid "$reader1_pid"
    stop_pid "$reader2_pid"

    if [ "$loaded_by_test" -eq 1 ] &&
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi

    rm -f "$POLL_LOG" "$EMPTY_POLL_LOG" "$READER1_LOG" "$READER2_LOG"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s 已在載入；test不會卸載非本次載入的module。\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
make
make -C "$ROOT_DIR/runtime"

$SUDO insmod "./${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_char_device "$DEVICE" \
    /sys/class/driver_lab_ctl/driver_lab_ctl0 \
    driver_lab_ctl

if [ "${DRIVER_LAB_COMPAT32:-0}" = "1" ]; then
    $SUDO "$ROOT_DIR/tests/run-compat32-ioctl.sh" lab03 "$DEVICE"
fi

# Basic control/data/shared paths.
$SUDO "$CLI" "$DEVICE" ioctl-write hello-ioctl
$SUDO "$CLI" "$DEVICE" status | grep 'buffer_len='
$SUDO "$CLI" "$DEVICE" read | grep 'hello-ioctl'
$SUDO "$CLI" "$DEVICE" mmap-read | grep 'magic=0x'

if command -v python3 >/dev/null 2>&1; then
    # The snapshot must reject both an initially writable VMA and a later
    # mprotect(PROT_WRITE) upgrade of a read-only mapping.
    $SUDO python3 - "$DEVICE" <<'PY'
import ctypes
import errno
import mmap
import os
import sys

path = sys.argv[1]
page_size = os.sysconf("SC_PAGE_SIZE")

fd = os.open(path, os.O_RDWR)
try:
    try:
        mapping = mmap.mmap(
            fd,
            page_size,
            flags=mmap.MAP_SHARED,
            prot=mmap.PROT_READ | mmap.PROT_WRITE,
        )
    except OSError as exc:
        if exc.errno not in (errno.EPERM, errno.EACCES):
            raise
    else:
        mapping.close()
        raise SystemExit("writable mmap unexpectedly succeeded")
finally:
    os.close(fd)

libc = ctypes.CDLL(None, use_errno=True)
libc.mmap.argtypes = [
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_long,
]
libc.mmap.restype = ctypes.c_void_p
libc.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
libc.mprotect.restype = ctypes.c_int
libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
libc.munmap.restype = ctypes.c_int

fd = os.open(path, os.O_RDONLY)
addr = libc.mmap(
    None,
    page_size,
    mmap.PROT_READ,
    mmap.MAP_SHARED,
    fd,
    0,
)
os.close(fd)
if addr == ctypes.c_void_p(-1).value:
    err = ctypes.get_errno()
    raise OSError(err, os.strerror(err))
try:
    ctypes.set_errno(0)
    if libc.mprotect(addr, page_size, mmap.PROT_READ | mmap.PROT_WRITE) == 0:
        raise SystemExit("mprotect(PROT_WRITE) unexpectedly succeeded")
    err = ctypes.get_errno()
    if err not in (errno.EPERM, errno.EACCES):
        raise OSError(err, os.strerror(err))
finally:
    if libc.munmap(addr, page_size) != 0:
        err = ctypes.get_errno()
        raise OSError(err, os.strerror(err))
PY

    # Record semantics: an undersized read must fail with EMSGSIZE and leave the
    # record pending for a later correctly sized reader.
    $SUDO "$CLI" "$DEVICE" write undersized-record-regression
    $SUDO python3 - "$DEVICE" <<'PY'
import errno
import os
import sys

fd = os.open(sys.argv[1], os.O_RDONLY)
try:
    try:
        os.read(fd, 4)
    except OSError as exc:
        if exc.errno != errno.EMSGSIZE:
            raise
    else:
        raise SystemExit("undersized read unexpectedly succeeded")
finally:
    os.close(fd)
PY
    $SUDO "$CLI" "$DEVICE" read | grep 'undersized-record-regression'
else
    printf 'SKIP: python3 unavailable; mmap/mprotect/record regressions skipped.\n' >&2
fi

# A positive event wakes poll and returns a nonzero ready mask.
$SUDO "$CLI" "$DEVICE" poll 3000 >"$POLL_LOG" &
poll_pid=$!
sleep 1
$SUDO "$CLI" "$DEVICE" trigger
wait "$poll_pid"
poll_pid=
grep 'poll ret=1' "$POLL_LOG"
grep -Eq 'revents=0x[1-9a-fA-F]' "$POLL_LOG"

# Clearing readiness must not be treated as a successful event; empty poll times out.
$SUDO "$CLI" "$DEVICE" clear
$SUDO "$CLI" "$DEVICE" poll 200 >"$EMPTY_POLL_LOG"
grep 'poll ret=0' "$EMPTY_POLL_LOG"
grep 'revents=0x0' "$EMPTY_POLL_LOG"

# Two blocking readers awakened for one record: exactly one consumes it; the
# other keeps waiting for the next record instead of returning EOF.
$SUDO timeout 5s "$CLI" "$DEVICE" read >"$READER1_LOG" 2>&1 &
reader1_pid=$!
$SUDO timeout 5s "$CLI" "$DEVICE" read >"$READER2_LOG" 2>&1 &
reader2_pid=$!
sleep 1

$SUDO "$CLI" "$DEVICE" write reader-message-one

i=0
while [ "$i" -lt 50 ]; do
    out1=0
    out2=0
    [ -s "$READER1_LOG" ] && out1=1
    [ -s "$READER2_LOG" ] && out2=1

    if [ $((out1 + out2)) -eq 1 ]; then
        break
    fi
    if [ $((out1 + out2)) -gt 1 ]; then
        printf 'ERROR: both readers returned after one record.\n' >&2
        cat "$READER1_LOG" "$READER2_LOG" >&2
        exit 1
    fi

    sleep 0.1
    i=$((i + 1))
done

if [ "$i" -ge 50 ]; then
    printf 'ERROR: no blocking reader consumed the first record.\n' >&2
    exit 1
fi

$SUDO "$CLI" "$DEVICE" write reader-message-two

reader1_status=0
reader2_status=0
wait "$reader1_pid" || reader1_status=$?
wait "$reader2_pid" || reader2_status=$?
reader1_pid=
reader2_pid=

if [ "$reader1_status" -ne 0 ] || [ "$reader2_status" -ne 0 ]; then
    printf 'ERROR: reader status: reader1=%d reader2=%d\n' \
        "$reader1_status" "$reader2_status" >&2
    cat "$READER1_LOG" "$READER2_LOG" >&2
    exit 1
fi

one_count=$(count_matches 'reader-message-one' "$READER1_LOG" "$READER2_LOG")
two_count=$(count_matches 'reader-message-two' "$READER1_LOG" "$READER2_LOG")
if [ "$one_count" -ne 1 ] || [ "$two_count" -ne 1 ]; then
    printf 'ERROR: message counts: first=%s second=%s\n' \
        "$one_count" "$two_count" >&2
    cat "$READER1_LOG" "$READER2_LOG" >&2
    exit 1
fi
if grep -q 'read 0 bytes' "$READER1_LOG" "$READER2_LOG"; then
    printf 'ERROR: a blocking reader incorrectly returned EOF.\n' >&2
    exit 1
fi

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "$DEVICE" "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
make clean

printf '03-ioctl-poll-mmap smoke and regression tests passed.\n'
