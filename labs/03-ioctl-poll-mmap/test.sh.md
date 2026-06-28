# `test.sh` 詳解

## 結論

`labs/03-ioctl-poll-mmap/test.sh` 是 Lab03 的 Linux smoke test。它驗證的不是單一函式，而是整條鏈：

```text
build .ko
build runtime + CLI
insmod
verify /dev + /sys + /proc/devices
run ioctl/read/mmap/poll CLI paths
rmmod
verify /dev + /sys disappear
clean build artifacts
```

它的核心價值是：確認 Lab03 的四條 userspace ABI path 真的能在 Linux kernel 上跑，而不只是 source 看起來合理。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- 共用 filesystem helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- CLI：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)。
- driver：[`driver_lab_ioctl_poll_mmap.c.md`](driver_lab_ioctl_poll_mmap.c.md)。
- POSIX shell 的 `trap`/shell 語意以目前 script 和 POSIX sh 常見語意解釋，不展開所有 shell 標準細節。

## 先理解這份檔案在 repo 的位置

這支 script 要在 Lab03 目錄執行，但它自己會計算路徑，所以你也可以從別處呼叫：

```sh
./labs/03-ioctl-poll-mmap/test.sh
```

它會使用：

| 檔案/工具 | 用途 |
|---|---|
| [`Makefile`](Makefile) | build Lab03 `.ko` |
| [`../../runtime/Makefile`](../../runtime/Makefile) | build runtime + CLI |
| [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c) | 操作 `/dev/driver_lab_ctl0` |
| [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh) | 驗證 `/dev`、`/sys`、`/proc/devices` |

## 這份檔案要解決什麼問題？

手動測 Lab03 容易漏步驟，例如：

- 忘了重建 runtime CLI。
- 舊 module 還載著。
- `/dev` 出現但 `/sys/class` 沒出現。
- `poll` 沒真的被 event 喚醒。
- `rmmod` 後 device node 殘留。

`test.sh` 把這些最小成功條件串起來。

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

`#!/bin/sh` 表示這支 script 要維持 POSIX sh 相容，不應隨便加入 Bash-only 語法。

`set -eu`：

| option | 意義 |
|---|---|
| `-e` | 命令失敗時中止 |
| `-u` | 使用未設定變數時中止 |

Linux guard 是必要的，因為 macOS 不能 build/load Linux kernel module，也沒有同樣的 `lsmod`、`insmod`、`rmmod` 行為。

## 二、路徑與測試狀態

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_ioctl_poll_mmap
SUDO=
POLL_LOG=$(mktemp)
poll_pid=
```

重點：

- `SCRIPT_DIR`：Lab03 目錄。
- `ROOT_DIR`：repo 根目錄。
- `MODULE_NAME`：`lsmod` / `rmmod` 用的 module name。
- `POLL_LOG`：存背景 poll output。
- `poll_pid`：記錄背景 poll process id。

`CDPATH=` 是為了避免使用者環境變數 `CDPATH` 影響 `cd` 輸出或行為。

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if [ -n "$poll_pid" ] && kill -0 "$poll_pid" 2>/dev/null; then
        kill "$poll_pid" 2>/dev/null || true
        wait "$poll_pid" 2>/dev/null || true
    fi
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$POLL_LOG"
}

trap cleanup EXIT INT TERM
```

cleanup 做三件事：

1. 如果背景 poll 還活著，殺掉並 wait。
2. 如果 module 還載著，卸載。
3. 刪掉 temp log。

`trap cleanup EXIT INT TERM` 保證正常結束、Ctrl-C、被終止時都會清理。

為什麼 cleanup 裡有 `|| true`？

因為 cleanup 是 best-effort。測試已經要結束了，不希望 kill/wait/rmmod 清理失敗造成 cleanup 自己中斷，留下更多殘留。

## 四、sudo 與共用 filesystem helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

如果不是 root，就用 `sudo` 執行需要權限的 module 操作。

`FS_SUDO=$SUDO` 是傳給 `fs-surface-checks.sh` 的設定，讓 helper 在需要檢查 root-only path 時能用同樣 sudo 策略。

`.` 是 source，不是執行子程序。這樣 `fs_expect_char_device`、`fs_expect_absent` 這些 function 會出現在目前 shell。

## 五、build module 與 runtime

原始碼：

```sh
cd "$SCRIPT_DIR"
make
make -C "$ROOT_DIR/runtime"
```

第一個 `make`：

```text
build labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.ko
```

第二個 `make`：

```text
build tests/driver_lab_char_cli
```

兩者都需要，因為測試不是只 load module，還會透過 CLI 操作 driver。

## 六、卸載舊 module，避免狀態污染

原始碼：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

如果前一次測試中斷，module 可能還載著。直接 `insmod` 同名 module 會失敗或造成狀態混亂，所以先檢查並卸載。

`grep -q "^${MODULE_NAME} "` 的空白很重要，避免 module name prefix 誤判。

## 七、載入 module 並驗證 filesystem surface

原始碼：

```sh
$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_ctl0 \
	/sys/class/driver_lab_ctl/driver_lab_ctl0 \
	driver_lab_ctl
```

`fs_expect_char_device` 不只看 `/dev` 存不存在，它也驗：

- `/dev/driver_lab_ctl0` 是 char device。
- `/sys/class/driver_lab_ctl/driver_lab_ctl0` 存在。
- sysfs `dev` 檔含 major:minor。
- `/proc/devices` 列出 `driver_lab_ctl`。

這比單純 `test -e /dev/...` 更有價值，因為它確認 char device registration 和 device model surface 都可觀測。

## 八、逐一驗證 CLI paths

原始碼：

```sh
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 ioctl-write hello-ioctl
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 status | grep 'buffer_len='
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 read | grep 'hello-ioctl'
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 mmap-read | grep 'magic=0x'
```

對照：

| 命令 | 驗證 |
|---|---|
| `ioctl-write hello-ioctl` | control path 能 set message |
| `status | grep buffer_len=` | status ioctl 有回傳 |
| `read | grep hello-ioctl` | data path 能讀回 message |
| `mmap-read | grep magic=0x` | mmap shared page 能讀 |

注意 `read` 會消費 buffer，所以後續 `mmap-read` 看到的 buffer 可能已清空；這個 test 只驗 mmap page 有正確 magic，不要求 buffer 仍是 `hello-ioctl`。

## 九、poll / trigger 測試

原始碼：

```sh
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 poll 3000 >"$POLL_LOG" &
poll_pid=$!
sleep 1
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 trigger
wait "$poll_pid"
poll_pid=
grep 'poll ret=1' "$POLL_LOG"
```

為什麼 `poll` 要放背景？

```text
poll 的目的就是等事件
如果前景執行 poll，script 就沒有機會執行 trigger
```

流程：

1. 背景啟動 `poll 3000`。
2. `sleep 1` 讓 poll 有時間進入等待。
3. 前景執行 `trigger`。
4. driver 喚醒 poll。
5. `grep 'poll ret=1'` 確認 poll 有收到事件。

## 十、clear / rmmod / 退場驗證

原始碼：

```sh
$SUDO "$ROOT_DIR/tests/driver_lab_char_cli" /dev/driver_lab_ctl0 clear
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_ctl0 "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
make clean
```

這裡驗證 cleanup 真的反映到 filesystem surface：

- `/dev/driver_lab_ctl0` 消失。
- `/sys/class/driver_lab_ctl/driver_lab_ctl0` 消失。

這能抓到 driver exit path 漏掉 `device_destroy()` 或 class/cdev cleanup 的問題。

## 常見卡點

- 在 macOS 跑會直接失敗，這是預期行為。
- `sudo` 可能要求密碼。
- `poll ret=0` 代表 timeout，先看 `trigger` 是否有執行。
- `mmap-read` 在 `read` 後只 grep magic，不 grep buffer，因為 read 會消費 buffer。
- `make clean` 不會卸載 module；卸載是前面的 `rmmod`。
- cleanup 裡的 `rmmod || true` 不代表忽略測試錯誤，而是收尾階段 best-effort。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 為什麼只能在 Linux 跑？ | 它需要 build/load/unload kernel module 並檢查 Linux `/dev`/`/sys`/`/proc`。 |
| 為什麼要同時 build module 和 runtime？ | module 提供 kernel driver，runtime build 產生 CLI。 |
| `fs_expect_char_device` 驗什麼？ | `/dev` char device、sysfs class device、sysfs dev major:minor、`/proc/devices`。 |
| 為什麼 poll 要背景跑？ | 前景需要執行 trigger 來喚醒 poll。 |
| `read` 後為什麼 `mmap-read` 不 grep `hello-ioctl`？ | 因為 Lab03 read 是消費型，完整讀完會清 buffer。 |
| `rmmod` 後為什麼還要 `fs_expect_absent`？ | 驗證 driver exit path 的 filesystem surface 真的退場。 |
