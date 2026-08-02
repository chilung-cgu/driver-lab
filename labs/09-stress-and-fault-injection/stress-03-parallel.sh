#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
MODULE_NAME=driver_lab_ioctl_poll_mmap
DEVICE=/dev/driver_lab_ctl0
SUDO=
pid0=
pid1=
pid2=
pid3=

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

stop_pid() {
    pid=$1
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup() {
    stop_pid "$pid0"
    stop_pid "$pid1"
    stop_pid "$pid2"
    stop_pid "$pid3"

    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    make -C "$LAB_DIR" clean >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

make -C "$LAB_DIR"
make -C "$ROOT_DIR/runtime"

# Do not silently unload a module that may belong to another test/session.
if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

$SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
fs_expect_char_device "$DEVICE" \
    /sys/class/driver_lab_ctl/driver_lab_ctl0 \
    driver_lab_ctl

worker() {
    idx=$1
    i=0

    while [ "$i" -lt 20 ]; do
        $SUDO "$CLI" "$DEVICE" ioctl-write "worker-$idx-$i" >/dev/null
        $SUDO "$CLI" "$DEVICE" status >/dev/null

        # A competing reader may consume the message first, so a bounded timeout
        # is expected. Only a successful read or GNU timeout's 124 status is
        # accepted; do not hide crashes, permission errors, or driver failures.
        read_status=0
        $SUDO timeout 2s "$CLI" "$DEVICE" read >/dev/null 2>&1 || read_status=$?
        case "$read_status" in
            0|124)
                ;;
            *)
                printf 'ERROR: worker %s read failed with status %s\n' \
                    "$idx" "$read_status" >&2
                return "$read_status"
                ;;
        esac

        $SUDO "$CLI" "$DEVICE" trigger >/dev/null
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

status0=0
status1=0
status2=0
status3=0
wait "$pid0" || status0=$?
wait "$pid1" || status1=$?
wait "$pid2" || status2=$?
wait "$pid3" || status3=$?
pid0=
pid1=
pid2=
pid3=

if [ "$status0" -ne 0 ] || [ "$status1" -ne 0 ] || \
   [ "$status2" -ne 0 ] || [ "$status3" -ne 0 ]; then
    printf 'ERROR: worker statuses: %s %s %s %s\n' \
        "$status0" "$status1" "$status2" "$status3" >&2
    exit 1
fi

$SUDO rmmod "$MODULE_NAME"
fs_expect_absent "$DEVICE" "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
printf 'stress-03-parallel passed.\n'
