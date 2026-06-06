# `driver_lab_runtime.c` 詳解

## 這份檔案的角色

這是 userspace runtime 的實作。它把 `open`、`close`、`read`、`write`、`ioctl`、`poll`、`mmap`、`munmap` 包成 `dl_runtime_*()` API，讓 CLI 不需要每次都直接處理 syscall 細節。

它位在 userspace，不是 kernel driver。它呼叫的是 Linux syscall，最後才透過 `/dev/driver_lab_ctl0` 進到 Lab03 driver 的 callback。

相關檔案：

- public API 宣告：[`../include/driver_lab_runtime.h.md`](../include/driver_lab_runtime.h.md)
- 共用 ABI：[`../include/driver_lab_uapi.h.md`](../include/driver_lab_uapi.h.md)
- 呼叫 runtime 的 CLI：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)
- 接收 syscall 的 kernel driver：[`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)

## 先讀哪裡

第一次照 syscall 類型分段讀：

1. `dl_runtime_open()` / `dl_runtime_open_flags()` / `dl_runtime_close()`
2. `dl_runtime_write()` / `dl_runtime_read()`
3. `dl_runtime_ioctl_set_message()` / `dl_runtime_ioctl_get_status()`
4. `dl_runtime_ioctl_trigger_event()` / `dl_runtime_ioctl_clear_buffer()`
5. `dl_runtime_poll_readable()`
6. `dl_runtime_mmap_shared()` / `dl_runtime_munmap_shared()`

讀的時候一直問：這個 helper 對應 driver 的 data path、control path、event path，還是 shared memory path？

## 主線資料流

```text
driver_lab_char_cli
  -> dl_runtime_* helper
  -> Linux syscall
  -> /dev/driver_lab_ctl0 fd
  -> Lab03 file_operations callback
```

runtime 的設計目標不是藏住 driver，而是把 userspace 端的 syscall 呼叫整理成可重用 API。這讓測試程式可以專注在「我要做哪個 driver 動作」，而不是每次都重寫 fd/error/ioctl struct/mmap 細節。

## 分區詳解

### include 區

`driver_lab_runtime.h` 會間接 include `driver_lab_uapi.h`，所以 runtime 可以使用：

- `struct dl_ioctl_message`
- `struct dl_ioctl_status`
- `struct dl_shared_page`
- `DL_IOC_SET_MESSAGE`
- `DL_IOC_GET_STATUS`
- `DL_IOC_TRIGGER_EVENT`
- `DL_IOC_CLEAR_BUFFER`
- `DL_MMAP_BYTES`

其他 system headers 分別提供 `errno`、`open` flags、`poll`、`ioctl`、`mmap`、`read/write/close`。

### open / close

`dl_runtime_open()` 只是用預設 `O_RDWR` 呼叫 `dl_runtime_open_flags()`。

`dl_runtime_open_flags()` 檢查 `handle` 和 `path` 後呼叫：

```c
handle->fd = open(path, flags);
```

成功後，`handle->fd` 就代表 userspace 持有的一個 device fd。

`dl_runtime_close()` 會在 `close()` 成功後把 `handle->fd` 設回 `-1`，避免 caller 誤用已關閉 fd。

### read / write

`dl_runtime_write()` 和 `dl_runtime_read()` 很薄，主要做參數檢查，然後原樣呼叫 syscall。

這裡不解讀 payload，因為 data path 的語意由 driver 決定。對 Lab03 來說，`write()` 會更新 message/event/shared page，`read()` 會讀回 message 並在完整讀完後消費 buffer。

### ioctl set message

`dl_runtime_ioctl_set_message()` 是 runtime 最有教學價值的 helper 之一。

userspace CLI 傳進來的是 C 字串，但 Lab03 UAPI 規定 `DL_IOC_SET_MESSAGE` 的 payload 是：

```c
struct dl_ioctl_message {
    char text[DL_MESSAGE_BYTES];
};
```

所以 runtime 會：

1. 檢查 handle/message。
2. 清空 `struct dl_ioctl_message msg`。
3. 用 `strlen()` 檢查是否超過固定 ABI 上限。
4. 把字串 copy 進 `msg.text`。
5. 呼叫 `ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg)`。

這就是 runtime 包裝 ABI 的具體例子。

### ioctl get status / trigger / clear

`dl_runtime_ioctl_get_status()` 把 caller 提供的 `struct dl_ioctl_status *` 交給 driver 填。

`dl_runtime_ioctl_trigger_event()` 和 `dl_runtime_ioctl_clear_buffer()` 沒有額外 payload，只是送一個 command number 給 driver。

這三個 helper 都維持 userspace convention：成功回 `0`，失敗回 `-1` 並讓 `errno` 描述原因。

### poll

`dl_runtime_poll_readable()` 建一個 `struct pollfd`：

- `fd = handle->fd`
- `events = POLLIN | POLLPRI`

`POLLIN` 對應一般可讀資料，`POLLPRI` 在 Lab03 用來代表 driver event pending。

如果 `poll()` 回傳成功且 caller 有提供 `revents`，runtime 會把 kernel 回報的 event mask 存回去。

### mmap / munmap

`dl_runtime_mmap_shared()` 呼叫：

```c
mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0)
```

成功後回傳 userspace pointer。CLI 會把它轉成 `struct dl_shared_page *` 讀 snapshot。

`dl_runtime_munmap_shared()` 則解除 mapping。注意 runtime 只檢查 addr/length，不驗證這個 addr 是否真的是前面 mmap 出來的 mapping；這是 caller 要維持的配對責任。

## 關鍵 API / 參數角色

| API | 參數角色 | 在 runtime 中的意義 |
|---|---|---|
| `open(path, flags)` | device path、open flags | 取得 `/dev/...` fd |
| `write(handle->fd, buf, count)` | fd、userspace source、長度 | 進入 driver `.write` |
| `read(handle->fd, buf, count)` | fd、userspace destination、長度 | 進入 driver `.read` |
| `ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg)` | fd、command、userspace payload pointer | 進入 driver `.unlocked_ioctl` |
| `poll(&pfd, 1, timeout_ms)` | fd array、fd 數量、timeout | 等待 driver 可讀或 event |
| `mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0)` | desired addr、大小、權限、shared/private、fd、offset | 映射 driver shared page |

## 常見卡點

- runtime 是 userspace code，不可以用 kernel helper，也不會直接碰 `struct file_operations`。
- userspace 失敗慣例是 `-1 + errno`；kernel callback 常見的是負 errno，例如 `-EINVAL`。
- `dl_runtime_ioctl_set_message()` 傳給 driver 的不是原始 `char *`，而是 `struct dl_ioctl_message *`。
- `poll()` 回傳值和 `revents` 不同：回傳值是有事件的 fd 數量，`revents` 是事件 mask。
- `mmap()` 成功後要搭配 `munmap()`，就像 `open()` 要搭配 `close()`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| runtime 的主要價值是什麼？ | 把 scattered syscall 和 UAPI struct 包成一致的 `dl_runtime_*()` API。 |
| `dl_runtime_ioctl_set_message()` 為什麼要建立 `struct dl_ioctl_message`？ | 因為 Lab03 UAPI 定義 ioctl payload 是固定大小 struct，不是原始 C 字串。 |
| `dl_runtime_poll_readable()` 等哪些事件？ | `POLLIN | POLLPRI`。 |
| `dl_runtime_mmap_shared()` 回傳的是 kernel pointer 嗎？ | 不是，是 userspace address space 裡的 mapping pointer。 |
