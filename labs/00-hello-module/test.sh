#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR"

SUDO=
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

make

if lsmod | grep -q '^driver_lab_hello '; then
    $SUDO rmmod driver_lab_hello
fi

$SUDO insmod ./driver_lab_hello.ko who=smoke-test repeat=2
$SUDO dmesg | tail -n 30 | grep 'driver_lab_hello'
$SUDO rmmod driver_lab_hello
make clean

printf '00-hello-module smoke test passed.\n'
