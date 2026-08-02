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

stop_pid() {
    pid=$1
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup() {
    stop_pid "$poll_pid"
    stop_pid "$reader1_pid"
    stop_pid "$reader2_pid"

    if lsmod | grep -q "^${MODULE_NAME} "; then
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

cd "$SCRIPT_DIR"
make
make -C "$ROOT_DIR/runtime"

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_char_device "$DEVICE" \
    /sys/class/driver_lab_ctl/driver_lab_ctl0 \
    driver_lab_ctl

# Basic control/data/shared paths.
$SUDO "$CLI" "$DEVICE" ioctl-write hello-ioctl
$SUDO "$CLI" "$DEVICE" status | grep 'buffer_len='
$SUDO "$CLI" "$DEVICE" read | grep 'hello-ioctl'
$SUDO "$CLI" "$DEVICE" mmap-read | grep 'magic=0x'

# The published snapshot is kernel -> userspace and must reject a writable VMA.
if command -v python3 >/dev/null 2>&1; then
    $SUDO python3 - "$DEVICE" <<'PY'
import errno
import mmap
import os
import sys

path = sys.argv[1]
fd = os.open(path, os.O_RDWR)
try:
    try:
        mapping = mmap.mmap(
            fd,
            os.sysconf("SC_PAGE_SIZE"),
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
PY
else
    printf 'SKIP: python3 not available; writable-mmap regression not run.\n' >&2
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

# Regression: two blocking readers awakened for one message. Exactly one may
# consume it; the other must keep waiting for the next publication, not return EOF.
$SUDO timeout 5s "$CLI" "$DEVICE" read >"$READER1_LOG" 2>&1 &
reader1_pid=$!
$SUDO timeout 5s "$CLI" "$DEVICE" read >"$READER2_LOG" 2>&1 &
reader2_pid=$!
sleep 1

$SUDO "$CLI" "$DEVICE" write reader-message-one

# Wait until one reader has produced output. If both finish on a single message,
# the post-wakeup condition was not rechecked correctly.
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
        printf 'ERROR: both readers returned after one message.\n' >&2
        cat "$READER1_LOG" "$READER2_LOG" >&2
        exit 1
    fi

    sleep 0.1
    i=$((i + 1))
done

if [ "$i" -ge 50 ]; then
    printf 'ERROR: no blocking reader consumed the first message.\n' >&2
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

cat "$READER1_LOG" "$READER2_LOG" | grep -c 'reader-message-one' | grep '^1$'
cat "$READER1_LOG" "$READER2_LOG" | grep -c 'reader-message-two' | grep '^1$'
if cat "$READER1_LOG" "$READER2_LOG" | grep -q 'read 0 bytes'; then
    printf 'ERROR: a blocking reader incorrectly returned EOF.\n' >&2
    exit 1
fi

$SUDO rmmod "$MODULE_NAME"
fs_expect_absent "$DEVICE" "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
make clean

printf '03-ioctl-poll-mmap smoke and regression tests passed.\n'
