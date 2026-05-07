#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR"

SUDO=
MESSAGE='hello-char-device'
READBACK_FILE=$(mktemp)
EXPECTED_FILE=$(mktemp)

cleanup() {
    rm -f "$READBACK_FILE"
    rm -f "$EXPECTED_FILE"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

make

if lsmod | grep -q '^driver_lab_char '; then
    $SUDO rmmod driver_lab_char
fi

$SUDO insmod ./driver_lab_char.ko
printf '%s' "$MESSAGE" | $SUDO tee /dev/driver_lab_char0 >/dev/null
$SUDO dd if=/dev/driver_lab_char0 of="$READBACK_FILE" bs=1 count=${#MESSAGE} status=none
printf '%s' "$MESSAGE" >"$EXPECTED_FILE"
diff -u "$EXPECTED_FILE" "$READBACK_FILE"
$SUDO dmesg | tail -n 50 | grep 'driver_lab_char'
$SUDO rmmod driver_lab_char
make clean

printf '02-char-device smoke test passed.\n'
