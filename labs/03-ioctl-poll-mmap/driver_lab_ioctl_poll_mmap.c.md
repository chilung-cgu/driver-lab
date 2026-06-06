# `driver_lab_ioctl_poll_mmap.c` 詳解

## 這份檔案的角色

這是 Lab03 的 kernel module 本體。它把 Lab02 的單純 char device 擴充成四條 userspace 可觀測路徑：

| 路徑 | userspace 動作 | driver callback | 你要學的重點 |
|---|---|---|---|
| data path | `read()` / `write()` | `dl_read()` / `dl_write()` | buffer 如何在 userspace 與 kernel 之間複製 |
| control path | `ioctl()` | `dl_unlocked_ioctl()` | 用 command number 控制 driver |
| event path | `poll()` | `dl_poll()` | waitqueue 如何讓 userspace 等事件 |
| shared memory path | `mmap()` | `dl_mmap()` | userspace 如何讀 driver 維護的 shared page |

這份檔案不是 userspace runtime。runtime 在 [`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md)，CLI 在 [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)，共用 ABI 在 [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md)。

## 先讀哪裡

第一次不要從第 1 行一路硬讀到最後。照這個順序比較順：

1. `dl_fops`：先確認 `/dev/driver_lab_ctl0` 支援哪些 callback。
2. `driver_lab_ioctl_poll_mmap_init()`：看 module 載入時建立哪些 resource。
3. `dl_write()` / `dl_read()`：先抓 data path。
4. `dl_unlocked_ioctl()`：看 control path 怎麼分派 command。
5. `dl_poll()`：看 userspace 等事件時掛到哪些 waitqueue。
6. `dl_mmap()`：看 shared page 如何被映射。
7. `driver_lab_ioctl_poll_mmap_exit()`：確認 cleanup 是否對稱。

## 主線資料流

```text
userspace CLI
  -> runtime helper
  -> syscall: write/read/ioctl/poll/mmap
  -> VFS 根據 file_operations 呼叫 dl_* callback
  -> driver 更新 dl_buffer / dl_event_count / dl_event_pending / shared page
  -> userspace 從 read/status/poll/mmap-read 觀察結果
```

Lab03 的核心狀態只有幾個：

- `dl_buffer`：目前 message。
- `dl_buffer_len`：message 長度。
- `dl_event_count`：事件累積次數。
- `dl_event_pending`：目前是否有 pending event。
- `dl_shared_page_addr`：給 `mmap()` 看的一頁 snapshot。

這些狀態由 `dl_lock` 保護。每次狀態改變後，`dl_sync_shared_page_locked()` 會把 snapshot 同步到 shared page。

## 分區詳解

### include 與全域 resource

`#include "../../runtime/include/driver_lab_uapi.h"` 是 Lab03 的關鍵。kernel driver 和 userspace runtime/CLI 都 include 同一份 UAPI header，所以雙方才會同意：

- `DL_MESSAGE_BYTES`
- `DL_MMAP_BYTES`
- `struct dl_ioctl_message`
- `struct dl_ioctl_status`
- `struct dl_shared_page`
- `DL_IOC_*` ioctl command

`dl_devt`、`dl_cdev`、`dl_class`、`dl_device` 是 char device 建立 pipeline。你可以把它們和 `/dev/driver_lab_ctl0`、`/sys/class/driver_lab_ctl/...` 對起來看。

### `dl_sync_shared_page_locked()`

這個 helper 把 driver 內部狀態整理成 `struct dl_shared_page`。名稱裡的 `_locked` 是重要提醒：呼叫者必須已經拿到 `dl_lock`。

它會清空 shared page，再寫入：

- magic/version：讓 userspace 確認 layout。
- event count/pending：讓 userspace 看到事件狀態。
- buffer length/content：讓 userspace 看到目前 message snapshot。

### `dl_publish_message_locked()`

這是 Lab03 的狀態更新集中點。`write()` 和 `DL_IOC_SET_MESSAGE` 都會走到這裡。

它做四件事：

1. 清掉舊 buffer。
2. 複製新 message。
3. 增加 `dl_event_count` 並設 `dl_event_pending = true`。
4. 同步 shared page。

這樣 data path 和 control path 不會各自更新一套語意。

### `dl_read()`

`dl_read()` 是 data path 的讀端。

如果 fd 是 `O_NONBLOCK` 且目前沒有資料，直接回 `-EAGAIN`。如果是 blocking fd，會用：

```c
wait_event_interruptible(dl_read_wq, READ_ONCE(dl_buffer_len) > 0);
```

等到 buffer 有資料。真正 copy 給 userspace 的動作交給 `simple_read_from_buffer()`。

讀完完整 message 後，這個 lab 採用「消費型」語意：清掉 buffer、清掉 pending event、更新 shared page，並喚醒 event waitqueue。

### `dl_write()`

`dl_write()` 是 data path 的寫端。

它先擋掉空寫入和過長 message，再用 `simple_write_to_buffer()` 從 userspace buffer 複製到 kernel local buffer。成功後呼叫 `dl_publish_message_locked()`，最後喚醒：

- `dl_read_wq`：讓 blocking read 醒來。
- `dl_event_wq`：讓 poll 看到 event。

### `dl_poll()`

`poll_wait()` 不是「立刻睡著」。它是把目前 file 和 waitqueue 掛進 poll 機制，之後 driver `wake_up_interruptible()` 時，userspace 的 `poll()` 才能醒。

Lab03 掛了兩個 waitqueue：

- `dl_read_wq`：有可讀資料時回 `POLLIN | POLLRDNORM`。
- `dl_event_wq`：有 pending event 時回 `POLLPRI`。

### `dl_unlocked_ioctl()`

這是 control path 的 command dispatcher。

| command | 行為 |
|---|---|
| `DL_IOC_SET_MESSAGE` | 從 userspace 複製 `struct dl_ioctl_message`，更新 message/event/shared page |
| `DL_IOC_GET_STATUS` | 把目前狀態整理成 `struct dl_ioctl_status` 回傳 userspace |
| `DL_IOC_TRIGGER_EVENT` | 不改 message，只增加 event count 並設 pending |
| `DL_IOC_CLEAR_BUFFER` | 清掉 buffer 與 pending event |

只要 `arg` 是 userspace pointer，就必須用 `copy_from_user()` 或 `copy_to_user()`，不能直接解參考。

### `dl_mmap()`

`mmap()` 只允許從 offset 0 映射最多一頁。它不是把任意 kernel memory 開給 userspace，而是把 driver 預先配置的 `dl_shared_page_addr` 透過 `remap_pfn_range()` 映射出去。

這裡第一輪先抓住三件事：

- `vma->vm_pgoff != 0` 會被拒絕。
- `size > PAGE_SIZE` 會被拒絕。
- userspace 看到的是 `struct dl_shared_page` layout。

### init / exit

`driver_lab_ioctl_poll_mmap_init()` 的 resource 順序是：

1. `__get_free_page()` 配 shared page。
2. `alloc_chrdev_region()` 拿 major/minor。
3. `cdev_init()` / `cdev_add()` 接上 `dl_fops`。
4. `class_create()` 建 sysfs class。
5. `device_create()` 建 device entry，通常也會讓 `/dev/driver_lab_ctl0` 出現。

失敗路徑用 `goto err_*` 逐層回收。`driver_lab_ioctl_poll_mmap_exit()` 則反向釋放 device、class、cdev、major/minor、shared page。

## 關鍵 API / 參數角色

| API | 參數角色 | 在這份檔案中的意義 |
|---|---|---|
| `simple_write_to_buffer(local, sizeof(local) - 1, &pos, buf, count)` | kernel destination、容量、offset、userspace source、長度 | 把 userspace message 複製到 kernel local buffer |
| `simple_read_from_buffer(buf, count, ppos, dl_buffer, dl_buffer_len)` | userspace destination、長度、offset、kernel source、source 長度 | 把 driver buffer 讀回 userspace |
| `copy_from_user(&msg, (void __user *)arg, sizeof(msg))` | kernel destination、userspace source、大小 | ioctl set-message 的 ABI struct 複製 |
| `copy_to_user((void __user *)arg, &status, sizeof(status))` | userspace destination、kernel source、大小 | ioctl get-status 的 ABI struct 回傳 |
| `poll_wait(file, &dl_read_wq, wait)` | fd context、waitqueue、poll context | 讓 userspace poll 能被 read waitqueue 喚醒 |
| `remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)` | VMA、userspace 起點、page frame、大小、權限 | 把 shared page 映射到 userspace |

## 常見卡點

- `poll_wait()` 不是直接睡眠；它是註冊等待點。
- `ioctl arg` 是 userspace pointer，不能直接當 kernel pointer 用。
- `mmap()` 看到的是 snapshot struct，不是把整個 driver state 暴露出去。
- `read()` 讀完會清 buffer，所以先 `read` 再 `mmap-read` 可能看到不同狀態。
- `_locked` helper 不是裝飾命名；它代表呼叫前必須持有 `dl_lock`。
- cleanup 順序要和 init resource acquisition 對稱。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `/dev/driver_lab_ctl0` 的 callback table 在哪裡？ | `dl_fops`。 |
| `write()` 和 `DL_IOC_SET_MESSAGE` 最後都集中到哪個 helper？ | `dl_publish_message_locked()`。 |
| `poll()` 為什麼能被 `trigger` 喚醒？ | `dl_poll()` 掛上 `dl_event_wq`，`DL_IOC_TRIGGER_EVENT` 會設 pending 並 `wake_up_interruptible(&dl_event_wq)`。 |
| shared page 的 layout 由哪份檔案定義？ | [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) 的 `struct dl_shared_page`。 |
| module unload 時必須釋放哪些主要 resource？ | device、class、cdev、major/minor、shared page。 |
