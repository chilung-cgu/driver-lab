#!/bin/sh
set -eu

# 這支 smoke test 驗證 00 的最小閉環：build -> insmod -> dmesg -> rmmod -> clean。
# 它必須在 Linux 上跑，因為 macOS 不能載入 Linux kernel module。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
MODULE_NAME=driver_lab_hello
SUDO=

cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
}

trap cleanup EXIT INT TERM

cd "$SCRIPT_DIR"

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

make

# 如果前一次測試留下同名 module，先卸載，避免 insmod 失敗。
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

# 載入 module 並傳入 module parameters，確認 dmesg 內有本 module log。
$SUDO insmod ./driver_lab_hello.ko who=smoke-test repeat=2
$SUDO dmesg | tail -n 30 | grep 'driver_lab_hello'
$SUDO rmmod "$MODULE_NAME"
make clean

printf '00-hello-module smoke test passed.\n'
