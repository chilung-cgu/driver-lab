# `stress-03-reload.sh` 詳解

## 結論

`labs/09-stress-and-fault-injection/stress-03-reload.sh` 是針對 Lab03 driver 的 repeated load/unload stress test。它把 Lab03 module 連續載入/卸載 20 次，每次都檢查：

```text
load 後 /dev/driver_lab_ctl0 存在
load 後 /sys/class/driver_lab_ctl/driver_lab_ctl0 存在
load 後 /proc/devices 列出 driver_lab_ctl
unload 後 /dev/driver_lab_ctl0 消失
unload 後 /sys/class/driver_lab_ctl/driver_lab_ctl0 消失
```

這支 script 的目標不是測單次功能，而是測 cleanup 對稱性。很多 driver bug 單跑一次不會出現，反覆 `insmod`/`rmmod` 才會暴露殘留 state 或 resource 沒釋放。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`stress-03-reload.sh`](stress-03-reload.sh) 本身。
- Lab09 suite runner：[`test.sh.md`](test.sh.md)。
- 共用 helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Lab03 driver/test 旁讀：[`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)、[`../03-ioctl-poll-mmap/test.sh.md`](../03-ioctl-poll-mmap/test.sh.md)。

這裡只解釋 repeated reload stress，不把它擴大成 memory fault injection 或 long-running soak test。

## 測試主線

流程：

```text
confirm Linux
compute repo/lab path
source fs-surface helper
register cleanup trap
make Lab03 module
repeat 20 times:
  remove stale module if needed
  insmod Lab03 module
  verify char device surfaces
  rmmod Lab03 module
  verify /dev and /sys entries are gone
print stress-03-reload passed
```

## 一、Linux guard

原始碼：

```sh
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi
```

這支 script 需要：

- Linux kernel module build tree。
- `insmod` / `rmmod`。
- `/dev`、`/sys/class`、`/proc/devices`。

macOS 不能直接執行。

## 二、路徑與 module name

原始碼：

```sh
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
MODULE_NAME=driver_lab_ioctl_poll_mmap
SUDO=
i=0
```

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab09 目錄。 |
| `ROOT_DIR` | repo 根目錄。 |
| `LAB_DIR` | Lab03 module 所在目錄。 |
| `MODULE_NAME` | Lab03 kernel module name。 |
| `i` | reload loop counter。 |

這支 script 雖然放在 Lab09，但實際 target 是 Lab03。

## 三、sudo 與 filesystem helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時，`insmod` / `rmmod` 需要 sudo。

這支 script 用 helper 驗證：

| helper | 驗證什麼 |
|---|---|
| `fs_expect_char_device` | `/dev` node、sysfs class device、`/proc/devices` 都存在且一致。 |
| `fs_expect_absent` | unload 後 `/dev` 與 sysfs entry 消失。 |

這比只看 `insmod` exit code 更強，因為它直接驗證使用者會看到的 filesystem surface。

## 四、cleanup 與 trap

原始碼：

```sh
cleanup() {
	if lsmod | grep -q "^${MODULE_NAME} "; then
		$SUDO rmmod "$MODULE_NAME" || true
	fi
	make -C "$LAB_DIR" clean >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM
```

如果 stress 中途失敗或被 Ctrl-C，cleanup 會：

1. 嘗試卸載 Lab03 module。
2. 清掉 Lab03 kbuild artifact。

`|| true` 是刻意的：cleanup 階段不應因為 module 已不在或 clean 失敗而遮蔽原本的錯誤。

## 五、build Lab03 module

原始碼：

```sh
make -C "$LAB_DIR"
```

這會在 Lab03 目錄建出：

```text
driver_lab_ioctl_poll_mmap.ko
```

reload stress 後面每一輪都用這個 `.ko` 做 `insmod`。

## 六、20 次 repeated load/unload

原始碼：

```sh
while [ "$i" -lt 20 ]; do
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME"
    fi

    $SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
    fs_expect_char_device /dev/driver_lab_ctl0 \
        /sys/class/driver_lab_ctl/driver_lab_ctl0 \
        driver_lab_ctl
    $SUDO rmmod "$MODULE_NAME"
    fs_expect_absent /dev/driver_lab_ctl0 "device node"
    fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
    i=$((i + 1))
done
```

每一輪做同樣的事：

```text
確保沒有舊 module
  -> insmod
  -> 檢查 char device surface
  -> rmmod
  -> 檢查 /dev 和 /sys 已消失
```

為什麼要 20 次？

```text
單次 smoke test
  只證明一次成功

20 次 load/unload
  更容易抓到 cleanup 不對稱、state 殘留、class/device destroy 漏掉
```

## 七、成功訊號

原始碼：

```sh
printf 'stress-03-reload passed.\n'
```

看到這行代表 20 次 loop 都完成，且每輪 load/unload 的 filesystem surface 檢查都通過。

## test 和 Lab03 的對照

| stress 檢查 | 對應 Lab03 行為 |
|---|---|
| `insmod` | module init 建立 char device/class/device node |
| `fs_expect_char_device` | `/dev/driver_lab_ctl0`、sysfs class device、`/proc/devices` |
| `rmmod` | module exit 清掉 cdev/device/class |
| `fs_expect_absent /dev/driver_lab_ctl0` | devtmpfs node 已消失 |
| `fs_expect_absent /sys/class/...` | device model entry 已消失 |

## 常見卡點

- 第 1 次就失敗：先跑 Lab03 `test.sh`，確認基本 smoke test 先通。
- 第 N 次才失敗：高度懷疑 cleanup path 不對稱或 state 殘留。
- unload 後 `/dev` 還在：看 `device_destroy()` / class cleanup path。
- unload 後 sysfs 還在：看 device/class lifecycle。
- `rmmod` 失敗：可能仍有 process 持有 fd，先用 `lsmod`、`lsof`、`dmesg` 查。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 script target 哪個 module？ | Lab03 的 `driver_lab_ioctl_poll_mmap`。 |
| loop 幾次？ | 20 次。 |
| 每次 load 後檢查什麼？ | `/dev/driver_lab_ctl0`、sysfs class device、`/proc/devices`。 |
| 每次 unload 後檢查什麼？ | `/dev/driver_lab_ctl0` 和 sysfs class device 已消失。 |
| repeated reload 主要抓哪類 bug？ | init/exit cleanup 不對稱、resource 或 filesystem surface 殘留。 |
