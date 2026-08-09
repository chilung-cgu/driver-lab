#!/bin/sh
set -eu

# 這支 smoke test 驗證 char device 最小資料路徑：write 到 /dev，再 read 回來比對。
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$SCRIPT_DIR"

MODULE_NAME=driver_lab_char
SUDO=
MESSAGE='hello-char-device'
TMP_DIR=$(mktemp -d)
READBACK_FILE="$TMP_DIR/readback"
EXPECTED_FILE="$TMP_DIR/expected"
loaded_by_test=0

cleanup() {
	if [ "$loaded_by_test" -eq 1 ] && \
	   lsmod | grep -q "^${MODULE_NAME} "; then
		$SUDO rmmod "$MODULE_NAME" || true
	fi
	rm -rf "$TMP_DIR"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"

make

if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s 已在載入；test 不會卸載非本次載入的 module。\n' \
        "$MODULE_NAME" >&2
    exit 1
fi

# 寫入固定訊息，再讀回檔案，比對 read/write 路徑是否一致。
$SUDO insmod ./driver_lab_char.ko
loaded_by_test=1
fs_expect_char_device /dev/driver_lab_char0 \
	/sys/class/driver_lab_char/driver_lab_char0 \
	driver_lab_char
printf '%s' "$MESSAGE" | $SUDO tee /dev/driver_lab_char0 >/dev/null
$SUDO dd if=/dev/driver_lab_char0 of="$READBACK_FILE" bs=1 count=${#MESSAGE} status=none
printf '%s' "$MESSAGE" >"$EXPECTED_FILE"
diff -u "$EXPECTED_FILE" "$READBACK_FILE"
$SUDO dmesg | tail -n 50 | grep 'driver_lab_char'
$SUDO rmmod "$MODULE_NAME"
loaded_by_test=0
fs_expect_absent /dev/driver_lab_char0 "device node"
fs_expect_absent /sys/class/driver_lab_char/driver_lab_char0 "sysfs class device"
make clean

printf '02-char-device smoke test passed.\n'
