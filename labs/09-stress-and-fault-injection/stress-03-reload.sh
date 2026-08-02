#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
MODULE_NAME=driver_lab_ioctl_poll_mmap
DEVICE=/dev/driver_lab_ctl0
ITERATIONS=${ITERATIONS:-20}
SUDO=
i=0
loaded_by_test=0

case "$ITERATIONS" in
    ''|*[!0-9]*|0)
        printf 'ERROR: ITERATIONS must be a positive integer.\n' >&2
        exit 1
        ;;
esac

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

cleanup() {
    if [ "$loaded_by_test" -eq 1 ] && \
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    make -C "$LAB_DIR" clean >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

# Do not remove a module that may belong to another user/test session.
if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

make -C "$LAB_DIR"

while [ "$i" -lt "$ITERATIONS" ]; do
    $SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
    loaded_by_test=1

    fs_expect_char_device "$DEVICE" \
        /sys/class/driver_lab_ctl/driver_lab_ctl0 \
        driver_lab_ctl
    lsmod | grep -q "^${MODULE_NAME} "

    $SUDO rmmod "$MODULE_NAME"
    loaded_by_test=0

    if lsmod | grep -q "^${MODULE_NAME} "; then
        printf 'ERROR: %s still present after iteration %d unload.\n' \
            "$MODULE_NAME" "$i" >&2
        exit 1
    fi
    fs_expect_absent "$DEVICE" "device node"
    fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 \
        "sysfs class device"

    i=$((i + 1))
done

printf 'stress-03-reload passed (%s iterations).\n' "$ITERATIONS"
