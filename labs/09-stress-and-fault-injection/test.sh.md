# `test.sh` 詳解

## 結論

`labs/09-stress-and-fault-injection/test.sh` 是 Lab09 目前的最小 stress suite 入口。它不是完整 fault-injection framework；目前只串起兩支 Lab03 專用 stress script：

```text
stress-03-reload.sh
stress-03-parallel.sh
```

所以這支檔案的角色是「suite runner」：

```text
先跑 repeated load/unload
再跑 parallel userspace access
兩個都通過才印 basic stress suite passed
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab09 README：[`README.md`](README.md)。
- reload stress：[`stress-03-reload.sh.md`](stress-03-reload.sh.md)。
- parallel stress：[`stress-03-parallel.sh.md`](stress-03-parallel.sh.md)。
- Lab03 driver 旁讀：[`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。

這裡不把 `test.sh` 說成完整 fault injection。repo 目前尚未自動化 `failslab`、`fail_page_alloc`、`fail_usercopy`、KUnit 或 kselftest。

## 原始碼全貌

```sh
#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)

"$SCRIPT_DIR/stress-03-reload.sh"
"$SCRIPT_DIR/stress-03-parallel.sh"

printf '09-stress-and-fault-injection basic stress suite passed.\n'
```

這支檔案很短，但它把 Lab09 的第一版驗證策略固定下來：

```text
單次 smoke test
  不夠

repeated reload + parallel access
  是第一層 stress/regression 習慣
```

## 一、`set -eu`

原始碼：

```sh
set -eu
```

| option | 意義 |
|---|---|
| `-e` | 任一 stress script 失敗時，suite 直接停止。 |
| `-u` | 使用未設定變數時失敗。 |

因此只要 reload 或 parallel 任一支失敗，Lab09 suite 就不會印最後的 passed 訊息。

## 二、找 script directory

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
```

這讓 suite 可以從任何 cwd 執行：

```sh
./labs/09-stress-and-fault-injection/test.sh
```

或：

```sh
cd labs/09-stress-and-fault-injection
./test.sh
```

都會找到同目錄的 stress scripts。

## 三、執行 reload stress

原始碼：

```sh
"$SCRIPT_DIR/stress-03-reload.sh"
```

這一段先跑 repeated load/unload。

它要抓的是：

```text
init/exit cleanup 不對稱
多次 insmod/rmmod 後殘留 /dev 或 /sys entry
module unload 沒有釋放乾淨
```

詳細流程見 [`stress-03-reload.sh.md`](stress-03-reload.sh.md)。

## 四、執行 parallel stress

原始碼：

```sh
"$SCRIPT_DIR/stress-03-parallel.sh"
```

reload 通過後，才跑 parallel userspace access。

它要提高這些路徑同時被碰到的機率：

```text
ioctl-write
status
read
trigger
```

詳細流程見 [`stress-03-parallel.sh.md`](stress-03-parallel.sh.md)。

## 五、成功訊號

原始碼：

```sh
printf '09-stress-and-fault-injection basic stress suite passed.\n'
```

看到這行代表：

```text
stress-03-reload.sh exit 0
stress-03-parallel.sh exit 0
```

但它不代表完整 fault injection 已經完成。這點 README 已經明確標出。

## test 和其他檔案的對照

| test 片段 | 對應旁讀 |
|---|---|
| `stress-03-reload.sh` | [`stress-03-reload.sh.md`](stress-03-reload.sh.md) |
| `stress-03-parallel.sh` | [`stress-03-parallel.sh.md`](stress-03-parallel.sh.md) |
| Lab03 target driver | [`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md) |
| Lab03 CLI/runtime | [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)、[`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md) |

## 常見卡點

- 以為這支 test 有做 `failslab`：目前沒有。
- 以為這支 test 是 generic stress framework：目前只針對 Lab03。
- reload stress 失敗：先看 init/exit cleanup 與 `/dev`、`/sys/class` 是否殘留。
- parallel stress 失敗：先看 worker 中哪個 CLI subcommand 失敗，再看 `dmesg`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab09 `test.sh` 目前串哪兩支 script？ | `stress-03-reload.sh` 與 `stress-03-parallel.sh`。 |
| 它是完整 fault-injection framework 嗎？ | 不是，目前是 Lab03 專用 basic stress suite。 |
| 為什麼先跑 reload 再跑 parallel？ | 先確認 load/unload cleanup 基礎穩定，再對 userspace access 施壓。 |
| 最後 passed 訊息代表什麼？ | 兩支 stress script 都 exit 0。 |
