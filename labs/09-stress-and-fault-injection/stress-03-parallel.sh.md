# `stress-03-parallel.sh` 詳解

## 結論

`stress-03-parallel.sh` 對 Lab03 driver 做可調整的 parallel userspace stress。它建 module 與 runtime CLI、載入一個由自己擁有的 module，然後讓 `WORKERS` 個 worker 各自跑 `ITERATIONS` 輪 `ioctl-write`、`status`、`mmap-read`、bounded `read`、`trigger`。

預設是 4 個 worker、每個 20 輪、read timeout 2 秒。`mmap-read` 的失敗會嚴格傳回；blocking read 只有成功的 `0` 或 GNU `timeout` 的 `124` 能被接受。這是 normal-path concurrency stress，不是完整 race detector 或 fault-injection suite。

## 第一輪先懂

- `WORKERS`、`ITERATIONS`、`READ_TIMEOUT_SECONDS` 都必須是正整數。
- 每個 worker 對同一個 Lab03 device 壓力測試，但寫入內容含 worker 與 iteration，方便追蹤。
- `mmap-read` 讀的是 driver 發布的 shared snapshot；它不能被忽略。
- `read` 是消費型路徑，別的 worker 可能先讀掉資料，所以只允許 exit `0` 或 `124`。
- 一個 worker 的其他任何錯誤都保留其 status，主 script 不會用 broad `|| true` 藏起來。
- 所有 worker、unload 與 filesystem cleanup 完成後，marker-scoped dmesg gate 通過才印 passed。

## 不確定處 / 查證範圍

本文件對照了：

- [`stress-03-parallel.sh`](stress-03-parallel.sh)；
- [`dmesg-gate.sh`](dmesg-gate.sh)；
- [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)；
- CLI 的 [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c)；
- Lab03 driver 的 [`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。

這個 script 沒有故意觸發 allocation、copy_to/from_user、signal interruption 或 fault-injection framework，因此通過不能被解讀為那些 error path 已驗證。

## 測試主線

```text
confirm Linux and positive parameters
→ build Lab03 module + runtime CLI
→ refuse a pre-loaded module
→ write a unique kernel-log marker
→ insmod and verify char-device surface
→ start W workers
→ each worker repeats N times:
     ioctl-write unique message
     status
     mmap-read shared snapshot
     bounded blocking read
     trigger
→ wait every worker and preserve the first observed worker failure status
→ rmmod and verify /dev + sysfs disappear
→ inspect only dmesg after the marker
→ print passed
```

## 參數

```sh
WORKERS=${WORKERS:-4}
ITERATIONS=${ITERATIONS:-20}
READ_TIMEOUT_SECONDS=${READ_TIMEOUT_SECONDS:-2}
```

例如：

```sh
WORKERS=8 \
ITERATIONS=100 \
READ_TIMEOUT_SECONDS=2 \
./stress-03-parallel.sh
```

每個值都由 `require_positive_integer` 檢查。空值、`0`、負數與非數字都在載入 module 前失敗；這讓 README 的參數契約與實際 script 一致。

`WORKERS` 與 `ITERATIONS` 放大同一組正常操作排程，並不產生新的 failure path。`READ_TIMEOUT_SECONDS` 是 timeout 的秒數；它不會把任意 read error 變成成功。

## Module ownership 與 cleanup

script 一開始若發現 module 已載入就失敗：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi
```

成功 `insmod` 後，`loaded_by_test=1` 才允許 cleanup 卸載它。background worker PID 會收集到 `worker_pids`；若 script 中途失敗或收到 signal，cleanup 會先停止並 reap 仍屬於本 script 的 worker，再清掉這次載入的 module。

`fs_expect_char_device` 驗證：

```text
/dev/driver_lab_ctl0
/sys/class/driver_lab_ctl/driver_lab_ctl0
/proc/devices -> driver_lab_ctl
```

成功 unload 後則以 `fs_expect_absent` 驗證 `/dev` 與 sysfs class device 都已消失。

## Worker 的五個操作

核心 loop：

```sh
while [ "$i" -lt "$ITERATIONS" ]; do
    $SUDO "$CLI" "$DEVICE" ioctl-write "worker-$idx-$i" >/dev/null
    $SUDO "$CLI" "$DEVICE" status >/dev/null
    $SUDO "$CLI" "$DEVICE" mmap-read >/dev/null

    read_status=0
    $SUDO timeout "${READ_TIMEOUT_SECONDS}s" "$CLI" "$DEVICE" read \
        >/dev/null 2>&1 || read_status=$?
    case "$read_status" in
        0|124) ;;
        *) return "$read_status" ;;
    esac

    $SUDO "$CLI" "$DEVICE" trigger >/dev/null
    i=$((i + 1))
done
```

| 操作 | 對應路徑 | 失敗處理 |
|---|---|---|
| `ioctl-write` | ioctl 寫入新的 record | 非零直接結束 worker。 |
| `status` | ioctl 取得狀態 | 非零直接結束 worker。 |
| `mmap-read` | mmap + shared snapshot validation | 非零直接結束 worker。 |
| `read` | 消費型 blocking read | 只接受 `0` 或 `124`。 |
| `trigger` | event ioctl | 非零直接結束 worker。 |

### 為什麼 `mmap-read` 必須嚴格失敗？

CLI 的 `mmap-read` 會讀 status、確認 `mmap_size` 和 userspace page size、mmap shared page、讀 snapshot，再驗證 magic/version/pending/buffer length。任何失敗都代表 shared snapshot path 沒有通過；因此不能像有競爭語意的 blocking read 一樣接受 timeout 或忽略。

### 為什麼 blocking read 只接受 `0` 或 `124`？

record 可能先被另一個 worker 消費。這時 GNU `timeout` 結束 read 並回傳 `124` 是預期可觀察結果。`EIO`、CLI usage error、permission error、timeout helper 本身的錯誤與 crash 都不是預期競爭結果，因此會以原 status 讓 worker 失敗。

## 動態 worker 與失敗傳遞

script 不再把 worker 寫死成四個 PID：

```sh
worker_index=0
while [ "$worker_index" -lt "$WORKERS" ]; do
    worker "$worker_index" &
    worker_pids="$worker_pids $!"
    worker_index=$((worker_index + 1))
done
```

每個 PID 都單獨 `wait`。若多個 worker 都失敗，script 依 PID list 順序記住第一個觀察到的非零 status 與其 PID，等待其餘 worker 後以該 status 結束。這保留具體 worker failure code，也避免 orphan worker 在 module unload 後還繼續執行；它不宣稱能判定不同 worker 的真實時間先後。

## Marker-scoped dmesg gate

在第一個 `insmod` 前開始：

```sh
dmesg_gate_begin "stress-03-parallel"
```

正常 path 要先 unload 與驗證 surface 消失，再完成 gate：

```sh
if ! dmesg_gate_check_and_cleanup; then
    exit 1
fi
printf 'stress-03-parallel passed ...\n'
```

詳細行為見 [`dmesg-gate.sh.md`](dmesg-gate.sh.md)。若主 workload 已失敗，EXIT cleanup 仍會擷取 marker 後的 kernel log，但保留原本 nonzero status；若主 workload 成功、gate 偵測到 diagnostics 或 marker 遺失，最終結果改為失敗。

## 限制 / 例外

- 這是有限次、normal-path 的 userspace concurrency pressure，不是 race-free proof。
- kernel log 是全機器共享的；marker 後的無關 warning 也會造成 failure。請在受控 Linux guest 保存完整 stdout/stderr。
- `124` 只表示某次 `read` 超時，不代表所有 worker 都讀到自己的訊息；那是此 test 的消費型 read 契約。
- 沒有 KUnit、kselftest、failslab、fail_page_alloc、fail_usercopy 或專用 fault injection fixture。

## 讀完後你應該能回答

| 問題 | 答案 |
|---|---|
| 怎麼把 worker 從 4 調成 8？ | `WORKERS=8 ./stress-03-parallel.sh`。 |
| `mmap-read` 失敗會被忽略嗎？ | 不會；它讓該 worker 以非零 status 結束。 |
| 為什麼 `read` 可接受 124？ | 另一個 worker 可能先消費唯一 record，bounded read 超時是預期競爭結果。 |
| 是否可以接受所有 `read` 非零結果？ | 不可以，只有 `0` 和 GNU `timeout` 的 `124`。 |
| 這是否已經是 fault-injection suite？ | 不是；目前只做 normal-path stress。 |
