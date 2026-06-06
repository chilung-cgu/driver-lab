# `test.sh` 詳解

## 結論

`labs/00-hello-module/test.sh` 是 Lab00 的 smoke test。它自動跑最小閉環：

```text
make
  -> insmod driver_lab_hello.ko who=smoke-test repeat=2
  -> dmesg grep driver_lab_hello
  -> rmmod
  -> make clean
```

這支 script 的價值是讓你確認第一個 kernel module 不是只會 build，而是真的能載入、印 log、卸載、清理。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab00 source：[`driver_lab_hello.c.md`](driver_lab_hello.c.md)。
- Lab00 build：[`Makefile.md`](Makefile.md)。
- Linux man-pages `insmod(8)`、`dmesg(1)`。

不展開 Secure Boot/module signing 的處理；如果 `insmod` 失敗，先依 [`debug-checklist.md`](debug-checklist.md) 查。

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

`#!/bin/sh` 表示保持 POSIX sh 相容。

`set -eu`：

| option | 意義 |
|---|---|
| `-e` | 命令失敗時中止 |
| `-u` | 使用未設定變數時中止 |

Linux guard 是必要的，因為 macOS 不能載入 Linux kernel module。

## 二、路徑與 module name

原始碼：

```sh
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
MODULE_NAME=driver_lab_hello
SUDO=
```

`SCRIPT_DIR` 讓 script 不受呼叫位置影響。`MODULE_NAME` 用於 `lsmod` / `rmmod`。

`SUDO` 會根據是否 root 決定要不要設成 `sudo`。

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

如果測試中途失敗，cleanup 會嘗試卸載已載入的 module。

`|| true` 是 best-effort cleanup；避免 cleanup 自己失敗後中斷更多收尾。

## 四、sudo 判斷

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
```

module load/unload 和 dmesg 通常需要權限。不是 root 時就使用 `sudo`。

## 五、build

原始碼：

```sh
make
```

這會透過 [`Makefile.md`](Makefile.md) 呼叫 kbuild，產生：

```text
driver_lab_hello.ko
```

## 六、卸載舊 module

原始碼：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

如果前一次測試中斷，module 可能還載在 kernel 裡。先卸載可以避免 `insmod` 因同名 module 已存在而失敗。

## 七、載入 module 並檢查 dmesg

原始碼：

```sh
$SUDO insmod ./driver_lab_hello.ko who=smoke-test repeat=2
$SUDO dmesg | tail -n 30 | grep 'driver_lab_hello'
```

這裡測兩件事：

1. `insmod` 能成功載入 `.ko`。
2. kernel log 裡看得到 `driver_lab_hello`。

傳入參數：

```text
who=smoke-test
repeat=2
```

所以預期 init 會印出 smoke-test 相關 log。

`insmod(8)` man page 也提醒，錯誤時 `dmesg` 通常會有更多資訊；所以這一關把 `dmesg` 當作主要觀測點是合理的。

## 八、卸載與 clean

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
make clean

printf '00-hello-module smoke test passed.\n'
```

`rmmod` 會觸發 `driver_lab_hello_exit()`，`make clean` 清 build artifact。

最後印出通過訊息。

## 常見卡點

- 在 macOS 跑會失敗，這是預期。
- `insmod` 失敗時，先看 `sudo dmesg | tail -n 50`。
- `grep 'driver_lab_hello'` 只確認 log 裡有 module name，不驗證每一行 hello 次數。
- `make clean` 不會卸載 module，所以 script 先 `rmmod`。
- 如果 sudo 需要密碼，自動化測試可能卡住。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 smoke test 驗證哪個閉環？ | build、insmod、dmesg、rmmod、clean。 |
| 為什麼只能在 Linux 跑？ | 需要載入 Linux kernel module。 |
| `who=smoke-test repeat=2` 會進到哪裡？ | `driver_lab_hello.c` 的 module parameters。 |
| `dmesg | grep driver_lab_hello` 驗什麼？ | 驗證 module init/exit 的 kernel log 可觀測。 |
| cleanup 的目的？ | 測試中斷時嘗試卸載殘留 module。 |

## 查證來源

- Linux man-pages `insmod(8)`：載入 kernel module；錯誤資訊通常看 `dmesg`。<https://man7.org/linux/man-pages/man8/insmod.8.html>
- Linux man-pages `dmesg(1)`：讀 kernel ring buffer。<https://man7.org/linux/man-pages/man1/dmesg.1.html>
