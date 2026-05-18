# 01 到 03：user-kernel ABI 過渡導讀

如果你剛讀完 `01-debugfs-logging`，接著看到 `02-char-device` 和 `03-ioctl-poll-mmap`，很容易覺得名詞突然暴增。這份文件先把最小心智模型補起來。

## 先回答：`01` 後可以直接進 `02` 嗎？

可以，但先接受一件事：

> 第一輪不需要完整理解 VFS 內部。你只要知道 userspace 對某個檔案路徑操作時，kernel 會把操作分派到 driver 登記的 callback。

在 `01`，那個路徑是 debugfs：

```text
/sys/kernel/debug/driver_lab_debugfs/status
/sys/kernel/debug/driver_lab_debugfs/trigger
```

在 `02/03`，那個路徑變成 device node：

```text
/dev/driver_lab_char0
/dev/driver_lab_ctl0
```

## `01` 和 `02` 最重要的差別

| 比較 | `01-debugfs-logging` | `02-char-device` |
|---|---|---|
| 入口 | debugfs 檔案 | `/dev/driver_lab_char0` |
| 用途 | debug 觀測與簡單控制 | 比較正式的 userspace data path |
| 主要 callback | `dl_status_open()`、`dl_trigger_write()` | `dl_char_open()`、`dl_char_read()`、`dl_char_write()` |
| 第一輪重點 | driver state 可以被 debugfs 讀出或觸發 | userspace `read/write` 會進 driver callback |

debugfs 不是穩定產品 ABI。它適合教學、觀測、debug，不適合承諾給產品 app 長期依賴。

## `/dev/driver_lab_char0` 是怎麼來的？

你可以先把 `02` 的 init path 想成 4 步：

1. `alloc_chrdev_region()`：向 kernel 申請一組 major/minor device number。
2. `cdev_add()`：把 `file_operations` 掛到這組 device number。
3. `class_create()`：建立一個 device class，讓 device model 知道這類裝置。
4. `device_create()`：建立 device，系統通常會透過 devtmpfs/udev 看到 `/dev/driver_lab_char0`。

第一輪不用深究 udev 規則。你只要知道：`/dev/driver_lab_char0` 不是普通文字檔，而是指向 driver callback 的入口。

## `file_operations` 要怎麼看？

`file_operations` 是 driver 給 VFS 的 callback 表。

在 `02`，先看這個對照：

| userspace 做什麼 | VFS 會找哪個欄位 | driver 函式 |
|---|---|---|
| `open("/dev/driver_lab_char0")` | `.open` | `dl_char_open()` |
| `read(fd, ...)` | `.read` | `dl_char_read()` |
| `write(fd, ...)` | `.write` | `dl_char_write()` |

`struct inode` 和 `struct file` 第一輪可以先當成 VFS 傳給 callback 的上下文。先不要追它們完整欄位。

## `copy_to_user()` / `copy_from_user()` 在解決什麼？

kernel space 和 userspace 是不同保護邊界。driver 不能把 userspace pointer 當成一般 kernel pointer 直接解參考。

所以：

- `copy_from_user()`：把 userspace 傳來的資料複製進 kernel buffer。
- `copy_to_user()`：把 kernel buffer 裡的資料複製回 userspace。

在 `02` 先記這句：

> `write()` 通常會用 `copy_from_user()`；`read()` 通常會用 `copy_to_user()`。

## 為什麼 `03` 又多出 `ioctl/poll/mmap`？

`read/write` 只適合最基本資料傳輸。真實 driver 常常還需要：

| 路徑 | 目的 | `03` 裡的例子 |
|---|---|---|
| data path | 搬一般資料 | `read()` / `write()` |
| control path | 下控制命令或讀狀態 | `ioctl` 的 `DL_IOC_*` |
| event path | 等事件，不要 busy loop | `poll()` |
| shared memory path | 讓 userspace 看到 driver 維護的 shared page | `mmap()` |

你第一次讀 `03` 時，不要逐行硬追。先把每個 CLI subcommand 對到其中一條路。

## `ioctl` 第一輪怎麼理解？

`ioctl` 是「控制命令通道」。

在這個 repo，command number 放在：

- [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h)

第一輪先記：

- UAPI header 是 kernel 與 userspace 都要同意的合約。
- `DL_IOC_SET_MESSAGE` 是設定訊息。
- `DL_IOC_GET_STATUS` 是讀狀態。
- `DL_IOC_TRIGGER_EVENT` 是觸發事件。
- `DL_IOC_CLEAR_BUFFER` 是清 buffer。

`_IOW/_IOR` 的 bit layout 可以先略過。

## `poll` 第一輪怎麼理解？

`poll()` 是讓 userspace 等事件。

如果沒有 `poll()`，app 可能會一直重複問：

```text
有資料了嗎？有資料了嗎？有資料了嗎？
```

這叫 busy loop，浪費 CPU。

`03` 的 `poll()` 會等：

- buffer 有資料可讀
- 或 event 被 trigger

driver 內部會用 waitqueue 把等待者掛起來，等狀態改變時再喚醒。

## `mmap` 第一輪怎麼理解？

`mmap()` 是把 driver 維護的一頁 shared page 映射給 userspace 讀。

第一輪先不要把它想成「把所有 kernel memory 暴露出去」。在 `03` 裡，它只是一頁受控的 snapshot page，內容包含：

- magic
- version
- event count
- event pending
- buffer length
- buffer snapshot

## 讀 source code 的建議順序

### `02-char-device`

1. 先找 `driver_lab_char_init()`，看 `/dev/driver_lab_char0` 怎麼建立。
2. 再找 `dl_char_fops`，看 `.read` / `.write` 接到哪裡。
3. 再看 `dl_char_write()` 與 `dl_char_read()`。
4. 最後看 `driver_lab_char_exit()` 是否反向 cleanup。

### `03-ioctl-poll-mmap`

1. 先找 `dl_fops`，確認五個 callback：`read/write/ioctl/poll/mmap`。
2. 再找 `dl_publish_message_locked()`，看共享狀態如何被更新。
3. 再看 `dl_unlocked_ioctl()`，理解 control path。
4. 再看 `dl_poll()`，理解 event path。
5. 最後看 `dl_mmap()`，理解 shared memory path。

## 第一輪你可以先略過

- `struct inode` / `struct file` 的完整內部欄位。
- `_IOW/_IOR` command number 的位元編碼細節。
- `poll_table` 內部。
- page fault、VMA、memory management 的完整流程。

## 你要能回答的最小問題

讀完 `01-03` 後，你至少要能回答：

| 問題 | 標準答案 |
|---|---|
| debugfs 和 `/dev` 都會進 driver callback，但用途有什麼不同？ | debugfs 是 debug 觀測/臨時控制入口，不是穩定產品 ABI；`/dev/...` char device 是 userspace 對 driver 做正式資料操作的入口。 |
| `/dev/driver_lab_char0` 的 `read/write` 分別接到哪個函式？ | `dl_char_fops` 裡 `.read = dl_char_read`、`.write = dl_char_write`；VFS 會把 userspace 的 `read()` / `write()` 分派到這兩個 callback。 |
| `03` 為什麼要把 path 分成 data/control/event/shared memory？ | 因為真實 driver 不只搬資料：data path 用 `read/write`，control path 用 `ioctl`，event path 用 `poll` 等事件，shared memory path 用 `mmap` 讓 userspace 讀 driver 維護的 shared page。 |
| `ioctl`、`poll`、`mmap` 各自解決什麼問題？ | `ioctl` 解控制命令與狀態查詢；`poll` 解等待事件且避免 busy loop；`mmap` 解讓 userspace 看到受控 shared page。 |
| 失敗時第一個看 `dmesg`、CLI output，還是 device node 是否存在？ | 先看失敗層次：`insmod`/callback 沒反應先看 `dmesg`；CLI command 失敗先看 CLI output；`/dev/...` 不存在先查 `insmod` 是否成功與 `ls -l /dev/...`。 |
