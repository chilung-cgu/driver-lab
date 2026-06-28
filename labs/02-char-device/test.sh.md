# `test.sh` 詳解

## 結論

`labs/02-char-device/test.sh` 是 Lab02 的 Linux smoke test。它驗證第一個 char device 的最小閉環：

```text
build driver_lab_char.ko
insmod
verify /dev/driver_lab_char0 is a char device
verify sysfs class device exists
verify /proc/devices lists driver_lab_char
write message to /dev/driver_lab_char0
read message back with dd
diff expected vs readback
grep dmesg
rmmod
verify /dev and sysfs entries are removed
make clean
```

這支 script 的價值不是只確認「可以 build」。它把 README 裡講的 filesystem surface 和 driver data path 變成可重複驗證的證據。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab02 driver：[`driver_lab_char.c.md`](driver_lab_char.c.md)。
- repo helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Linux man-pages：`dd(1)`、`diff(1)`、`dmesg(1)`、`read(2)`、`write(2)`。

這裡不展開 shell `set -e` 的所有 corner case，也不展開 devtmpfs/udev 的完整流程；只解釋這支 smoke test 如何驗 Lab02 的核心行為。

## 一、shell 模式與 Linux guard

原始碼：

```sh
#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi
```

這支 test 必須在 Linux 上跑，因為它要：

- build Linux kernel module。
- `insmod` / `rmmod`。
- 操作 `/dev/driver_lab_char0`。
- 讀 `/sys/class/...`、`/proc/devices`、`dmesg`。

`set -eu`：

| option | 意義 |
|---|---|
| `-e` | 命令失敗時中止 script。 |
| `-u` | 使用未設定變數時中止 script。 |

## 二、路徑、module name、暫存檔

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
cd "$SCRIPT_DIR"

MODULE_NAME=driver_lab_char
SUDO=
MESSAGE='hello-char-device'
TMP_DIR=$(mktemp -d)
READBACK_FILE="$TMP_DIR/readback"
EXPECTED_FILE="$TMP_DIR/expected"
```

重點：

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab02 目錄；讓 script 可從任意目錄執行。 |
| `ROOT_DIR` | repo 根目錄；用來 source 共用 helper。 |
| `MODULE_NAME` | 給 `lsmod` / `rmmod` 使用。 |
| `MESSAGE` | 固定測試 payload。 |
| `TMP_DIR` | 保存 readback 與 expected 檔案，用完清掉。 |

`cd "$SCRIPT_DIR"` 很重要，因為後面 `make`、`insmod ./driver_lab_char.ko` 都假設目前在 Lab02 目錄。

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
	if lsmod | grep -q "^${MODULE_NAME} "; then
		$SUDO rmmod "$MODULE_NAME" || true
	fi
	rm -rf "$TMP_DIR"
}

trap cleanup EXIT INT TERM
```

如果測試中途失敗，cleanup 會：

1. 嘗試卸載殘留的 `driver_lab_char` module。
2. 刪掉 temporary directory。

`|| true` 代表 cleanup 是 best effort。測試失敗時，最重要的是不要讓 cleanup 自己的失敗蓋掉原始問題。

## 四、sudo 與 filesystem helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時，用 `sudo` 跑需要權限的動作。

`FS_SUDO` 傳給 [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。source 之後，本 script 可以直接使用：

- `fs_expect_char_device`
- `fs_expect_absent`

## 五、build 與清掉舊 module

原始碼：

```sh
make

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

`make` 會透過 [`Makefile.md`](Makefile.md) 建出：

```text
driver_lab_char.ko
```

如果前一次測試中斷，module 可能還在 kernel 裡。先 `rmmod` 可以避免這次 `insmod` 因同名 module 已存在而失敗。

## 六、insmod 與 char device surface 檢查

原始碼：

```sh
$SUDO insmod ./driver_lab_char.ko
fs_expect_char_device /dev/driver_lab_char0 \
	/sys/class/driver_lab_char/driver_lab_char0 \
	driver_lab_char
```

`insmod` 成功後，driver init path 應該已經跑完：

```text
alloc_chrdev_region()
cdev_init()
cdev_add()
class_create()
device_create()
```

`fs_expect_char_device` 會檢查三層證據：

| 檢查 | 意義 |
|---|---|
| `/dev/driver_lab_char0` 存在且 `test -c` 成功 | userspace 入口存在，而且它是 char device，不只是普通檔案。 |
| `/sys/class/driver_lab_char/driver_lab_char0/dev` 存在且格式是 `major:minor` | kernel device model 有對應 device。 |
| `/proc/devices` 裡有 `driver_lab_char` | major/minor 名稱已被註冊。 |

如果這裡失敗，先看：

```sh
sudo dmesg | tail -n 50
ls -l /sys/class/driver_lab_char
ls -l /dev/driver_lab_char0
grep driver_lab_char /proc/devices
```

## 七、write path：把 payload 寫進 driver

原始碼：

```sh
printf '%s' "$MESSAGE" | $SUDO tee /dev/driver_lab_char0 >/dev/null
```

對 userspace 來說，這是對 `/dev/driver_lab_char0` 做 `write()`。

對 driver 來說，VFS 會走：

```text
dl_char_open()
dl_char_write()
dl_char_release()
```

`dl_char_write()` 會把 `MESSAGE` 複製到 `dl_char_buffer`，更新 `dl_char_buffer_len`，並把 file offset 重設為 0。

## 八、read path：用 `dd` 讀回固定長度

原始碼：

```sh
$SUDO dd if=/dev/driver_lab_char0 of="$READBACK_FILE" bs=1 count=${#MESSAGE} status=none
printf '%s' "$MESSAGE" >"$EXPECTED_FILE"
diff -u "$EXPECTED_FILE" "$READBACK_FILE"
```

這段分三步：

1. `dd` 從 device node 讀 `${#MESSAGE}` bytes 到 `READBACK_FILE`。
2. `printf` 建立 expected file。
3. `diff -u` 比對兩者。

為什麼不用 `cat /dev/driver_lab_char0`？

因為這支 driver 的 read path 使用檔案式 offset。`cat` 也可以讀，但 smoke test 更想精準控制「讀幾個 bytes」，避免測試依賴工具對 EOF 與 block size 的行為。

如果 `diff` 失敗，代表：

- `write()` 沒有正確保存 payload。
- `read()` 沒有正確回傳 payload。
- 或 read/write 的 offset/長度語意跟測試預期不同。

## 九、dmesg、rmmod、退場驗證

原始碼：

```sh
$SUDO dmesg | tail -n 50 | grep 'driver_lab_char'
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_char0 "device node"
fs_expect_absent /sys/class/driver_lab_char/driver_lab_char0 "sysfs class device"
make clean
```

`dmesg grep` 確認 kernel log 有 Lab02 driver 的觀測訊息，例如：

```text
created /dev/driver_lab_char0
wrote ... bytes
read ... bytes
device removed
```

`rmmod` 後檢查 `/dev` 和 sysfs entry 消失，是為了驗證 cleanup path：

```text
device_destroy()
class_destroy()
cdev_del()
unregister_chrdev_region()
```

最後 `make clean` 只清 build artifact，不負責卸載 module。所以順序必須是先 `rmmod`，再 `make clean`。

## test 和 source 的對照

| test 片段 | 驗證的 source path |
|---|---|
| `insmod ./driver_lab_char.ko` | `driver_lab_char_init()` |
| `fs_expect_char_device ...` | `alloc_chrdev_region()`、`cdev_add()`、`class_create()`、`device_create()` 的外部效果 |
| `tee /dev/driver_lab_char0` | `dl_char_write()` |
| `dd if=/dev/driver_lab_char0` | `dl_char_read()` |
| `diff -u expected readback` | write/read data path 是否一致 |
| `rmmod "$MODULE_NAME"` | `driver_lab_char_exit()` |
| `fs_expect_absent ...` | cleanup 是否移除 `/dev` 與 sysfs surface |

## 常見卡點

- 在 macOS 跑會直接失敗，這是預期；Lab02 必須在 Linux 上跑。
- 如果 `sudo` 需要密碼，非互動式環境可能卡住。
- `/dev/driver_lab_char0` 存在但不是 char device，`fs_expect_char_device` 會抓出來。
- `/sys/class/.../dev` 沒有 `major:minor`，代表 device model surface 不符合預期。
- `diff` 失敗時，優先回頭看 `dl_char_write()` / `dl_char_read()`，不是先改測試。
- `make clean` 不會卸載已載入 module。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 smoke test 驗證哪個閉環？ | build、insmod、filesystem surface、write、read、diff、dmesg、rmmod、cleanup、clean。 |
| 為什麼要用 `fs_expect_char_device`？ | 同時確認 `/dev` node、sysfs class device、`/proc/devices` 三層證據。 |
| `tee /dev/driver_lab_char0` 對應 driver 哪個 callback？ | `dl_char_write()`。 |
| `dd if=/dev/driver_lab_char0` 對應 driver 哪個 callback？ | `dl_char_read()`。 |
| `diff -u` 驗證什麼？ | 寫入 payload 和讀回 payload 完全一致。 |
| `rmmod` 後檢查什麼？ | `/dev/driver_lab_char0` 與 `/sys/class/driver_lab_char/driver_lab_char0` 已消失。 |

## 查證來源

- Linux man-pages `dd(1)`：以指定 block size/count 複製輸入到輸出。<https://man7.org/linux/man-pages/man1/dd.1.html>
- Linux man-pages `diff(1)`：比較 expected/readback 檔案。<https://man7.org/linux/man-pages/man1/diff.1.html>
- Linux man-pages `dmesg(1)`：讀 kernel ring buffer。<https://man7.org/linux/man-pages/man1/dmesg.1.html>
- Linux man-pages `read(2)`、`write(2)`：userspace read/write syscall 語意。<https://man7.org/linux/man-pages/man2/read.2.html>、<https://man7.org/linux/man-pages/man2/write.2.html>
