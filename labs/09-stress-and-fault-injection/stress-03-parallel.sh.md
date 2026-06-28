# `stress-03-parallel.sh` 詳解

## 結論

`labs/09-stress-and-fault-injection/stress-03-parallel.sh` 是針對 Lab03 driver 的 parallel userspace access stress test。它載入 Lab03 module，建出 runtime CLI，然後啟動 4 個 worker 同時反覆呼叫：

```text
ioctl-write
status
read
trigger
```

目標是提高 read/write/ioctl/poll/event/shared state 被同時碰到的機率。它不是在證明效能，也不是完整 race detector；它是第一層可重複的並行壓力驗證。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`stress-03-parallel.sh`](stress-03-parallel.sh) 本身。
- Lab09 suite runner：[`test.sh.md`](test.sh.md)。
- 共用 helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Lab03 driver 旁讀：[`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。
- CLI/runtime 旁讀：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)、[`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md)。

這裡不把它說成完整 concurrency proof。它只是提高並行使用下暴露問題的機率。

## 測試主線

流程：

```text
confirm Linux
compute repo/lab/CLI paths
source fs-surface helper
register cleanup trap
make Lab03 module
make runtime CLI
remove stale module if needed
insmod Lab03 module
verify char device surfaces
start 4 workers in background
each worker loops 20 times:
  ioctl-write message
  status
  read with timeout guard
  trigger
wait all workers
rmmod
verify /dev and /sys entries are gone
print stress-03-parallel passed
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

- Linux kernel module load/unload。
- Lab03 device node。
- Runtime CLI 實際操作 `/dev/driver_lab_ctl0`。

macOS 不能直接跑。

## 二、路徑、CLI 與 module name

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
MODULE_NAME=driver_lab_ioctl_poll_mmap
SUDO=
```

| 變數 | 用途 |
|---|---|
| `LAB_DIR` | Lab03 module 目錄。 |
| `CLI` | runtime build 出來的 userspace CLI。 |
| `MODULE_NAME` | Lab03 kernel module name。 |

這支 script 的壓力來源不是自己手寫 syscall，而是透過 CLI/runtime 進入 driver。

## 三、cleanup 與 trap

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

失敗或中斷時，cleanup 會嘗試卸載 Lab03 module 並清 Lab03 build artifact。

注意：runtime CLI build artifact 由 `runtime/Makefile` 管理。這支 script 目前只清 Lab03 module artifact，符合它的 target 範圍。

## 四、build module 與 runtime CLI

原始碼：

```sh
make -C "$LAB_DIR"
make -C "$ROOT_DIR/runtime"
```

這裡要兩個產物：

| build | 產物 |
|---|---|
| `make -C "$LAB_DIR"` | `driver_lab_ioctl_poll_mmap.ko` |
| `make -C "$ROOT_DIR/runtime"` | `tests/driver_lab_char_cli` |

沒有 CLI，就沒有 parallel userspace client；沒有 `.ko`，driver path 不存在。

## 五、載入 Lab03 並確認 filesystem surface

原始碼：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_ctl0 \
	/sys/class/driver_lab_ctl/driver_lab_ctl0 \
	driver_lab_ctl
```

先移除舊 module，避免前一輪殘留狀態干擾。

`fs_expect_char_device` 確認三個 surface：

```text
/dev/driver_lab_ctl0
/sys/class/driver_lab_ctl/driver_lab_ctl0
/proc/devices -> driver_lab_ctl
```

這三個成立後，才開始 parallel workers。

## 六、worker：每個 userspace client 做什麼？

原始碼：

```sh
worker() {
	idx=$1
	i=0
	while [ "$i" -lt 20 ]; do
		$SUDO "$CLI" /dev/driver_lab_ctl0 ioctl-write "worker-$idx-$i" >/dev/null
		$SUDO "$CLI" /dev/driver_lab_ctl0 status >/dev/null
		timeout 2s $SUDO "$CLI" /dev/driver_lab_ctl0 read >/dev/null || true
		$SUDO "$CLI" /dev/driver_lab_ctl0 trigger >/dev/null
		i=$((i + 1))
	done
}
```

每個 worker 跑 20 輪。每輪做：

| CLI subcommand | 對應 Lab03 path | 目的 |
|---|---|---|
| `ioctl-write` | ioctl set message | 寫入不同 worker/message。 |
| `status` | ioctl get status | 讀 control path 狀態。 |
| `read` | read path | 消費目前 message/data。 |
| `trigger` | event path | 觸發 poll/event 狀態。 |

`read` 前面加：

```sh
timeout 2s ... || true
```

原因是 read 是消費型語意。parallel worker 可能被其他 worker 先讀走資料，這時某個 worker 的 read 可能等不到資料。這裡的目標是 stress，不是讓一個預期競爭造成整個 script 永久卡住。

## 七、啟動 4 個 worker

原始碼：

```sh
worker 0 &
pid0=$!
worker 1 &
pid1=$!
worker 2 &
pid2=$!
worker 3 &
pid3=$!

wait "$pid0" "$pid1" "$pid2" "$pid3"
```

`&` 讓 worker 背景執行，`$!` 保存最新 background process PID。

`wait` 會等四個 worker 都結束。如果其中任何一個 worker 回傳失敗，`wait` 會讓 script 在 `set -e` 下失敗。

這就是 parallel stress 的核心：

```text
4 workers * 20 loops
  -> 80 組 ioctl/status/read/trigger 壓力
```

## 八、unload 與 cleanup 驗證

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_ctl0 "device node"
fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 "sysfs class device"
printf 'stress-03-parallel passed.\n'
```

parallel workers 完成後卸載 module，並確認 `/dev` 和 sysfs class device 消失。

這很重要：parallel access 之後，cleanup 仍要乾淨。否則表示壓力測試留下了 fd/lifetime/resource 問題。

## test 和 Lab03/runtime 的對照

| stress 片段 | 對應行為 |
|---|---|
| `make -C "$LAB_DIR"` | 建 Lab03 kernel module |
| `make -C "$ROOT_DIR/runtime"` | 建 runtime CLI |
| `fs_expect_char_device` | 確認 driver filesystem surface |
| `ioctl-write` | runtime -> ioctl set message |
| `status` | runtime -> ioctl get status |
| `read` | runtime -> read path |
| `trigger` | runtime -> ioctl/event path |
| `timeout 2s ... read || true` | 避免消費型 read 在 parallel race 下永久卡住 |
| `wait pid0 pid1 pid2 pid3` | 確認所有 background workers 完成 |

## 常見卡點

- CLI build 失敗：先跑 Lab08 `test.sh`。
- `fs_expect_char_device` 失敗：先跑 Lab03 `test.sh`。
- `read` timeout：這裡可能是可接受的競爭情境，所以有 `|| true`。
- worker 中其他 command 失敗：要看是哪個 subcommand，通常要查 CLI output 和 `dmesg`。
- `rmmod` 失敗：可能還有 worker 或其他 process 持有 fd，先查 process/fd。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 script 啟動幾個 worker？ | 4 個。 |
| 每個 worker 跑幾輪？ | 20 輪。 |
| 每輪打哪些 CLI subcommand？ | `ioctl-write`、`status`、`read`、`trigger`。 |
| 為什麼 read 用 `timeout 2s ... || true`？ | read 是消費型語意，parallel worker 可能互相搶資料；timeout 防止永久卡住。 |
| parallel stress 主要想抓什麼？ | 多個 userspace client 同時打 driver 時的共享狀態、等待路徑、cleanup 問題。 |
