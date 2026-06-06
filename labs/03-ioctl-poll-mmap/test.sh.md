# `test.sh` 詳解

## 這份檔案的角色

這是 Lab03 的 smoke test。它不嘗試證明 driver 產品級完整，而是確認 Lab03 的四條主線都能在 Linux 上跑通：

- `ioctl` control path
- `read` data path
- `mmap` shared memory path
- `poll` event path

它測的是 [`driver_lab_ioctl_poll_mmap.c`](driver_lab_ioctl_poll_mmap.c)、[`../../runtime/src/driver_lab_runtime.c`](../../runtime/src/driver_lab_runtime.c)、[`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c) 串在一起後的最小行為。

## 先讀哪裡

第一次照這個順序看：

1. 開頭的 Linux 檢查：確認為什麼 macOS 不能跑這支。
2. `cleanup()` 與 `trap`：先看失敗時怎麼卸載 module、刪 temp file。
3. `make` / `make -C "$ROOT_DIR/runtime"`：看 kernel module 與 userspace CLI 都會被重建。
4. `insmod` 後的 `fs_expect_char_device`：看 filesystem surface 驗證。
5. CLI 命令序列：看四條 ABI path 怎麼被驗證。
6. `rmmod` 後的 `fs_expect_absent`：看 cleanup 是否真的反映到 `/dev` / `/sys`。

## 主線資料流

```text
test.sh
  -> build Lab03 .ko
  -> build runtime + CLI
  -> insmod driver_lab_ioctl_poll_mmap.ko
  -> verify /dev and /sys entries
  -> run CLI subcommands through runtime
  -> verify output with grep
  -> rmmod
  -> verify /dev and /sys entries disappear
```

這支 test 的價值在於它從 userspace 的角度驗證整條鏈，而不是只檢查 `.ko` 能不能 build。

## 分區詳解

### Linux guard

```sh
if [ "$(uname -s)" != "Linux" ]; then
```

kernel module build/load、`lsmod`、`insmod`、`rmmod`、`/dev`/`/sys` surface 都是 Linux 行為，所以 macOS 只能做靜態檢查，不能跑這支 smoke test。

### path 與 temp state

`SCRIPT_DIR` 是 Lab03 目錄，`ROOT_DIR` 是 repo 根目錄。這讓 test 不管從哪個工作目錄啟動，都能找到 runtime、CLI 與共用 helper。

`POLL_LOG=$(mktemp)` 用來存背景 `poll` 的輸出。`poll_pid` 讓 cleanup 能在失敗時殺掉還在背景等待的 poll process。

### cleanup 與 trap

`cleanup()` 做三件事：

1. 如果背景 poll 還活著，先 kill/wait。
2. 如果 module 還載著，嘗試 `rmmod`。
3. 刪掉 temp log。

`trap cleanup EXIT INT TERM` 保證正常結束、Ctrl-C、或被終止時都會清理。這對 kernel module lab 很重要，因為殘留 module 會讓下一次測試狀態混亂。

### sudo 與 filesystem helper

如果不是 root，`SUDO=sudo`。接著：

```sh
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

`fs-surface-checks.sh` 提供 `/dev`、`/sys`、`/proc/devices` 等 surface 的共用檢查。Lab03 用它確認 char device 出現和消失。

### build 與 module load

```sh
make
make -C "$ROOT_DIR/runtime"
```

第一個 `make` 建 Lab03 kernel module。第二個 `make` 建 userspace runtime + CLI，因為後面的命令不是直接呼叫 syscall，而是透過 `driver_lab_char_cli` 和 runtime 驗證。

載入前若同名 module 已存在，先 `rmmod`，避免前一次測試殘留影響這次結果。

### filesystem surface 驗證

```sh
fs_expect_char_device /dev/driver_lab_ctl0 \
    /sys/class/driver_lab_ctl/driver_lab_ctl0 \
    driver_lab_ctl
```

這不是單純看 `/dev` 檔案是否存在，而是同時檢查：

- `/dev/driver_lab_ctl0` 是 char device。
- `/sys/class/driver_lab_ctl/driver_lab_ctl0` 存在。
- `/proc/devices` 裡有 `driver_lab_ctl`。

這些訊號合起來代表 char device registration 和 device model entry 都有成功。

### CLI 行為驗證

| 命令 | 驗證重點 |
|---|---|
| `ioctl-write hello-ioctl` | control path 能把 message 寫進 driver |
| `status | grep 'buffer_len='` | `DL_IOC_GET_STATUS` 能回傳結構化狀態 |
| `read | grep 'hello-ioctl'` | data path 能讀回剛設定的 message |
| `mmap-read | grep 'magic=0x'` | shared page 可以被 mmap 並讀到 magic |
| 背景 `poll 3000` + `trigger` | event path 可以被 waitqueue 喚醒 |
| `clear` | control path 可以清掉 buffer/event state |

### poll 測試為什麼要背景執行

`poll 3000` 的目的就是等待事件。如果前景直接執行，script 會卡在那裡，沒有機會送 `trigger`。所以 test 先把 poll 放背景：

```sh
"$ROOT_DIR/tests/driver_lab_char_cli" ... poll 3000 >"$POLL_LOG" &
```

接著 sleep 一秒，讓 poll 真的進入等待，再用 `trigger` 喚醒它。最後用 `grep 'poll ret=1'` 確認 poll 收到一個事件。

### unload 與退場驗證

`rmmod` 後，test 會檢查：

- `/dev/driver_lab_ctl0` 消失。
- `/sys/class/driver_lab_ctl/driver_lab_ctl0` 消失。

這能抓到「module 看似卸載，但 device node/sysfs entry 殘留」這類 cleanup 問題。

## 關鍵 shell pattern

| pattern | 用途 |
|---|---|
| `set -eu` | 未定義變數或命令失敗時中止 |
| `trap cleanup EXIT INT TERM` | 保證失敗時清理 module/temp file |
| `SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)` | 取得穩定絕對路徑，避免呼叫位置影響測試 |
| `SUDO=sudo` / `FS_SUDO=$SUDO` | 非 root 時仍能執行 module 與 filesystem 檢查 |
| `wait "$poll_pid"` | 等背景 poll 結束並取得結果 |

## 常見卡點

- 在 macOS 執行會直接失敗，因為這支需要 Linux kernel module 行為。
- `sudo` 可能要求密碼；自動化環境要先處理 sudo 權限。
- 如果前一次測試異常中斷，舊 module 可能仍載著；這支會先卸載同名 module。
- `poll ret=0` 通常代表 timeout，先看 `trigger` 是否真的執行成功。
- `/dev` 存在不代表完整成功，還要看 sysfs class 和 `/proc/devices`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 為什麼只能在 Linux 跑？ | 它需要 build/load/unload kernel module，並驗證 Linux `/dev`、`/sys`、`/proc` surface。 |
| 為什麼要同時 `make` 和 `make -C ../../runtime`？ | 前者建 `.ko`，後者建 userspace runtime + CLI。 |
| 背景 poll 的目的？ | 讓 poll 先睡著等待事件，主流程再 trigger 喚醒它。 |
| `fs_expect_absent` 在驗什麼？ | 驗證 `rmmod` 後 device node 和 sysfs class device 真的消失。 |
