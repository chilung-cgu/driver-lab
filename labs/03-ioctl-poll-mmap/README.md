# 03 - ioctl / poll / read-only mmap snapshot

## 目標

把 `02-char-device` 的最小 `read/write` 介面，擴成四條常見 driver ABI 路徑：

| 路徑 | userspace 呼叫 | driver callback | 本 lab 用途 |
|---|---|---|---|
| data | `read/write` | `.read/.write` | 傳遞 message |
| control | `ioctl` | `.unlocked_ioctl` | 設定、查狀態、觸發事件 |
| event | `poll` | `.poll` + waitqueue | 沒事件時睡眠等待 |
| shared snapshot | `mmap` | `.mmap` | 讀取 kernel 發布的狀態快照 |

這一關仍是教學 ABI，不是產品級介面。

## 先備條件

- 已理解 `/dev/...`、VFS 與 `file_operations`。
- 已完成 `02-char-device` 的 read/write round-trip。
- 知道 userspace pointer 必須經 `copy_*_user` 或相應 helper。

## 提供的介面

模組載入後建立：

```text
/dev/driver_lab_ctl0
```

支援：

- `write()`：發布一筆 message。
- `read()`：以消費型語意讀出目前 message；完整讀完後清空。
- `DL_IOC_SET_MESSAGE`：經 ioctl 發布 message。
- `DL_IOC_GET_STATUS`：取得 buffer/event 狀態與實際 `PAGE_SIZE`。
- `DL_IOC_TRIGGER_EVENT`：只建立事件，不建立可讀 message。
- `DL_IOC_CLEAR_BUFFER`：清空 message 與 pending event。
- `poll()`：等待 readable data 或 pending event。
- `mmap()`：建立 **read-only** shared-page mapping。

## 關鍵修正後的語意

### 1. blocking read 必須「醒來後再檢查」

Waitqueue condition 成立到取得 mutex 之間，另一個 reader 可能先消費 message。因此 driver 在拿到 `dl_lock` 後會再次檢查 `dl_buffer_len`：

```text
wait_event_interruptible()
        ↓
mutex_lock()
        ↓
重新確認 buffer 是否仍有資料
        ↓
有資料才 copy；否則 blocking reader 回去等
```

這避免第二個 reader 把競爭結果誤當成 EOF。

### 2. wake-up 不代表 `poll()` 一定返回

`wake_up_interruptible()` 只讓 poll core 重新評估 readiness。若重評估後 mask 仍是 0，blocking `poll()` 會繼續等待，不會因為一次 wake-up 就成功返回 `revents=0`。

因此本 lab 只在狀態變成 ready 時喚醒：

| 操作 | `dl_read_wq` | `dl_event_wq` | 可能的 readiness |
|---|---:|---:|---|
| `write` / `ioctl-write` | ✓ | ✓ | `POLLIN` + `POLLPRI` |
| `trigger` | — | ✓ | `POLLPRI` |
| `clear` | — | — | 狀態由 ready 變 not-ready，不需喚醒 |
| read 完整消費 | — | — | 狀態由 ready 變 not-ready，不需喚醒 |

### 3. mmap 是 read-only snapshot，不是任意共享寫入

本 lab 的 shared page 是 kernel 發布給 userspace 觀測的 snapshot：

- userspace 以 `PROT_READ` 映射；要求 writable mapping 會失敗；
- kernel 用 `alloc_page()` 配一個正常 RAM page；
- `.mmap` 用 `vm_insert_page()` 映射該 page；
- mapping 長度由 `DL_IOC_GET_STATUS.mmap_size` 回報，不能假設所有平台都是 4096-byte page。

### 4. mutex 不能保護 userspace reader

Kernel 的 `dl_lock` 只能序列化 kernel callbacks；userspace 直接讀 mmap page 時不會拿到這把鎖。為避免讀到半更新資料，shared layout 有 `seq`：

```text
偶數 seq：穩定 snapshot
奇數 seq：kernel 正在更新
```

runtime 的 `dl_runtime_read_shared_snapshot()` 會：

1. 讀起始 `seq`；
2. 若是奇數就重試；
3. 複製整份 snapshot；
4. 再讀一次 `seq`；
5. 只有兩次相同且為偶數才接受。

這是教學版 sequence-counter publication protocol；真實 ABI 還要考慮版本相容、權限與更完整的 lifetime 管理。

## Source 旁讀

| Source | Companion |
|---|---|
| [`driver_lab_ioctl_poll_mmap.c`](driver_lab_ioctl_poll_mmap.c) | [`driver_lab_ioctl_poll_mmap.c.md`](driver_lab_ioctl_poll_mmap.c.md) |
| [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) | [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md) |
| [`../../runtime/src/driver_lab_runtime.c`](../../runtime/src/driver_lab_runtime.c) | [`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md) |
| [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c) | [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md) |

## 使用方式

```sh
make
make -C ../../runtime
sudo insmod ./driver_lab_ioctl_poll_mmap.ko

../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 ioctl-write hello-03
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 mmap-read
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 read

# terminal A
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 poll 3000
# terminal B（在 timeout 前）
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 trigger

sudo rmmod driver_lab_ioctl_poll_mmap
```

## 自動化 smoke test

```sh
./test.sh
```

最低驗收：

- `/dev/driver_lab_ctl0` 存在；
- ioctl/write 發布的資料可 read；
- `poll` 能被 message 或 trigger 喚醒；
- `mmap-read` 取得 magic/version 正確且穩定的 snapshot；
- writable mmap 被拒絕；
- unload 後 device/class/page 都被清理。

## 建議追加的併發測試

同時啟動兩個 blocking reader，再發布一筆 message：

- 只有一個 reader 應消費該 message；
- 另一個 reader 應繼續等待下一筆，而不是錯誤回 EOF。

另外可在 writer 高頻更新時重複執行 `mmap-read`，確認 sequence retry 不會接受奇數或變動中的 snapshot。

## 完成後應該能回答

1. `poll_wait()` 做了什麼？為什麼它本身不是「在 callback 裡睡著」？
2. 為什麼 waitqueue condition 在拿 mutex 後還要再檢查？
3. 為什麼 kernel mutex 無法直接保證 userspace mmap reader 看到一致 snapshot？
4. 為什麼 UAPI 不能把 mmap size 永久寫死為 4096？
5. 為什麼本 lab 的 mapping 應是 read-only，而不是讓 userspace 改 kernel state？

## 限制

- 這個 shared page 只是狀態快照，不是高吞吐 ring buffer。
- 沒有 per-open state、權限模型、compat ioctl、ABI negotiation 或 hot-unplug。
- sequence protocol 不適合包含可被 writer 釋放的 pointer。
- 真實硬體資料路徑通常還要處理 DMA ownership、cache coherency、memory ordering 與 reset。
