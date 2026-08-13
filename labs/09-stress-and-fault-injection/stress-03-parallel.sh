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
WORKERS=${WORKERS:-4}
ITERATIONS=${ITERATIONS:-20}
READ_TIMEOUT_SECONDS=${READ_TIMEOUT_SECONDS:-2}
SUDO=
worker_pids=
loaded_by_test=0

require_positive_integer() {
    name=$1
    value=$2

    case "$value" in
        ''|*[!0-9]*|0)
            printf 'ERROR: %s must be a positive integer.\n' "$name" >&2
            return 1
            ;;
    esac
}

require_positive_integer WORKERS "$WORKERS"
require_positive_integer ITERATIONS "$ITERATIONS"
require_positive_integer READ_TIMEOUT_SECONDS "$READ_TIMEOUT_SECONDS"

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
DMESG_GATE_SUDO=$SUDO
. "$SCRIPT_DIR/dmesg-gate.sh"

stop_workers() {
    for pid in $worker_pids; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    for pid in $worker_pids; do
        wait "$pid" 2>/dev/null || true
    done
    worker_pids=
}

cleanup() {
    status=$?

    trap - EXIT INT TERM
    stop_workers

    if [ "$loaded_by_test" -eq 1 ] && \
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    make -C "$LAB_DIR" clean >/dev/null 2>&1 || true

    if [ "$DMESG_GATE_STARTED" -eq 1 ] && ! dmesg_gate_check_and_cleanup; then
        if [ "$status" -eq 0 ]; then
            status=1
        fi
    fi
    exit "$status"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

make -C "$LAB_DIR"
make -C "$ROOT_DIR/runtime"

# Do not silently unload a module that may belong to another test/session.
if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

dmesg_gate_begin "stress-03-parallel"
$SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_char_device "$DEVICE" \
    /sys/class/driver_lab_ctl/driver_lab_ctl0 \
    driver_lab_ctl

worker() {
    idx=$1
    i=0

    while [ "$i" -lt "$ITERATIONS" ]; do
        $SUDO "$CLI" "$DEVICE" ioctl-write "worker-$idx-$i" >/dev/null
        $SUDO "$CLI" "$DEVICE" status >/dev/null
        $SUDO "$CLI" "$DEVICE" mmap-read >/dev/null

        # A competing reader may consume the message first, so a bounded timeout
        # is expected. Only a successful read or GNU timeout's 124 status is
        # accepted; do not hide crashes, permission errors, or driver failures.
        read_status=0
        $SUDO timeout "${READ_TIMEOUT_SECONDS}s" "$CLI" "$DEVICE" read \
            >/dev/null 2>&1 || read_status=$?
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

worker_index=0
while [ "$worker_index" -lt "$WORKERS" ]; do
    worker "$worker_index" &
    worker_pids="$worker_pids $!"
    worker_index=$((worker_index + 1))
done

worker_status=0
worker_failure_pid=
for pid in $worker_pids; do
    if wait "$pid"; then
        :
    else
        wait_status=$?
        if [ "$worker_status" -eq 0 ]; then
            worker_status=$wait_status
            worker_failure_pid=$pid
        fi
    fi
done
worker_pids=

if [ "$worker_status" -ne 0 ]; then
    printf 'ERROR: worker pid %s failed with status %s\n' \
        "$worker_failure_pid" "$worker_status" >&2
    exit "$worker_status"
fi

$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent "$DEVICE" "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
if ! dmesg_gate_check_and_cleanup; then
    exit 1
fi
printf 'stress-03-parallel passed (%s workers, %s iterations, %ss read timeout).\n' \
    "$WORKERS" "$ITERATIONS" "$READ_TIMEOUT_SECONDS"
