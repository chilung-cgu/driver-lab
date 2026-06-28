# `test.sh` 詳解

## 結論

`labs/01-debugfs-logging/test.sh` 是 Lab01 的 Linux smoke test。它驗證 debugfs path 能真的在 kernel 上工作：

```text
mount debugfs
build .ko
insmod
verify debugfs files exist
cat status
write trigger
read trigger_count
optionally enable dynamic debug
grep dmesg
rmmod
verify debugfs directory removed
make clean
```

這支 script 的價值是確認「debugfs entry 出現、write path 有跑、state 有變、log 可觀測、cleanup 會退場」。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab01 driver：[`driver_lab_debugfs_logging.c.md`](driver_lab_debugfs_logging.c.md)。
- repo helper：[`../../scripts/mount-debugfs.sh`](../../scripts/mount-debugfs.sh)、[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Linux kernel documentation：debugfs、dynamic debug。

沒有展開 dynamic debug query language 的完整語法；只解釋本 script 使用的 `module driver_lab_debugfs_logging +p`。

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

Lab01 必須在 Linux 上跑，因為它需要：

- build/load kernel module。
- mount/read/write debugfs。
- 讀 `/proc/dynamic_debug/control`。
- 看 `dmesg`。

## 二、路徑、module name、sudo

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
MODULE_NAME=driver_lab_debugfs_logging
SUDO=
```

`SCRIPT_DIR` 是 Lab01 目錄，`ROOT_DIR` 是 repo 根目錄。這讓 script 可以從任何目錄呼叫。

後面：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時用 `sudo`。`FS_SUDO` 傳給 filesystem helper。`.` 會 source helper function 到目前 shell。

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
}

trap cleanup EXIT INT TERM
```

如果測試中途失敗，cleanup 會嘗試卸載 module，避免 debugfs entry 和 loaded module 殘留。

## 四、mount debugfs

原始碼：

```sh
"$ROOT_DIR/scripts/mount-debugfs.sh"
```

Lab01 需要 `/sys/kernel/debug`。如果 debugfs 沒掛載，後面不會看到 `/sys/kernel/debug/driver_lab_debugfs`。

把 mount 動作放在共用 script，是為了後面 labs 也能重用。

## 五、build 與卸載舊 module

原始碼：

```sh
cd "$SCRIPT_DIR"
make

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

先 build `driver_lab_debugfs_logging.ko`，再卸載可能殘留的同名 module，避免 `insmod` 失敗。

## 六、載入 module 並驗 debugfs files

原始碼：

```sh
$SUDO insmod ./driver_lab_debugfs_logging.ko
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/status
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/trigger
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/trigger_count
fs_expect_debugfs_file /sys/kernel/debug/driver_lab_debugfs/emit_debug
```

這段確認 driver init path 真的建立了四個 debugfs entries。

如果這裡失敗，先看：

```sh
mount | grep debugfs
sudo dmesg | tail -n 50
```

## 七、dynamic debug control 是 optional

原始碼：

```sh
fs_note_optional_path /proc/dynamic_debug/control "dynamic debug control"
```

有些 kernel 可能沒有 dynamic debug control。這不是 Lab01 必然失敗條件，所以用 optional note。

後面也有 guard：

```sh
if [ -e /proc/dynamic_debug/control ]; then
	...
fi
```

## 八、讀 status、寫 trigger、讀 trigger_count

原始碼：

```sh
$SUDO cat /sys/kernel/debug/driver_lab_debugfs/status
printf '%s' 'smoke-one' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
$SUDO cat /sys/kernel/debug/driver_lab_debugfs/trigger_count
```

對照 driver：

| 命令 | driver path |
|---|---|
| `cat status` | `dl_status_show()` |
| `tee trigger` | `dl_trigger_write()` |
| `cat trigger_count` | `debugfs_create_u32()` 導出的 `dl_trigger_count` |

這裡沒有 grep `trigger_count` 的精確值；它主要確認路徑可讀、write path 沒失敗。

## 九、啟用 dynamic debug 後再 trigger

原始碼：

```sh
if [ -e /proc/dynamic_debug/control ]; then
	echo 'module driver_lab_debugfs_logging +p' | $SUDO tee /proc/dynamic_debug/control >/dev/null
    printf '%s' 'smoke-two' | $SUDO tee /sys/kernel/debug/driver_lab_debugfs/trigger >/dev/null
fi
```

`module driver_lab_debugfs_logging +p` 是 dynamic debug query command，意思是打開這個 module 的 debug print callsites。

再寫一次 `trigger`，讓 `pr_debug()` path 有機會被觸發。

注意：如果 kernel 沒有 dynamic debug control，這段會跳過，不代表測試失敗。

## 十、dmesg、rmmod、退場驗證

原始碼：

```sh
$SUDO dmesg | tail -n 50 | grep 'driver_lab_debugfs_logging'
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /sys/kernel/debug/driver_lab_debugfs "debugfs directory"
make clean
```

`dmesg grep` 確認 module 有 log。`rmmod` 後 `fs_expect_absent` 確認 debugfs root 被移除。

這能抓到 exit path 忘記 `debugfs_remove(dl_root)` 的問題。

## 常見卡點

- debugfs 沒 mount，導致 `/sys/kernel/debug/driver_lab_debugfs` 不存在。
- 寫 `trigger` 沒用 sudo，權限不足。
- 沒有 `/proc/dynamic_debug/control` 不一定是錯；script 會跳過 dynamic debug 部分。
- `emit_debug=1` 不等於 `pr_debug()` 一定可見，還要 dynamic debug control 打開。
- `dmesg` 可能需要 sudo。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 為什麼先跑 `mount-debugfs.sh`？ | 確保 `/sys/kernel/debug` 已掛載。 |
| 它驗證哪些 debugfs files？ | `status`、`trigger`、`trigger_count`、`emit_debug`。 |
| dynamic debug control 不存在時測試會失敗嗎？ | 不會，該段是 optional。 |
| `tee trigger` 對應 driver 哪個 callback？ | `dl_trigger_write()`。 |
| `rmmod` 後檢查什麼？ | `/sys/kernel/debug/driver_lab_debugfs` 已消失。 |

## 查證來源

- Linux kernel documentation `DebugFS`：debugfs mount 與 debugfs interface 背景。<https://docs.kernel.org/filesystems/debugfs.html>
- Linux kernel documentation `Dynamic debug`：`/proc/dynamic_debug/control` 與 module `+p`。<https://docs.kernel.org/admin-guide/dynamic-debug-howto.html>
