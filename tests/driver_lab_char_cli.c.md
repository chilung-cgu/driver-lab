# `driver_lab_char_cli.c` 詳解

## 這份檔案的角色

這是 userspace 教學 CLI。它把命令列 subcommand 對到 runtime API，讓你可以從 shell 操作 driver 的不同路徑。

它不是 kernel driver，也不是 runtime 本體。它是 runtime 的 caller：

- runtime API 宣告：[`../runtime/include/driver_lab_runtime.h.md`](../runtime/include/driver_lab_runtime.h.md)
- runtime 實作：[`../runtime/src/driver_lab_runtime.c.md`](../runtime/src/driver_lab_runtime.c.md)
- Lab03 driver：[`../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)

## 先讀哪裡

第一次照 subcommand 讀，不要先追所有 C 細節：

1. `usage()`：列出 CLI 支援哪些操作。
2. `main()` 開頭：看 argv 形狀和 open flags。
3. `write` / `read`：data path。
4. `ioctl-write` / `status` / `clear`：control path。
5. `trigger` / `poll`：event path。
6. `mmap-read`：shared memory path。
7. 最後的 `dl_runtime_close()`：看 fd 生命週期收尾。

## 主線資料流

```text
shell command
  -> driver_lab_char_cli argv parsing
  -> dl_runtime_* helper
  -> syscall
  -> /dev/driver_lab_ctl0
  -> Lab03 driver callback
  -> CLI printf/perror 讓 test.sh grep 成功訊號
```

Lab03 的 [`test.sh.md`](../labs/03-ioctl-poll-mmap/test.sh.md) 就是靠這支 CLI 逐一驗證 ioctl/read/mmap/poll。

## 分區詳解

### `usage()`

`usage()` 是這支 CLI 的命令表。每一列都對應一種 driver path：

| subcommand | driver path |
|---|---|
| `write <message>` | data path `.write` |
| `read` | data path `.read` |
| `ioctl-write <message>` | control path `DL_IOC_SET_MESSAGE` |
| `status` | control path `DL_IOC_GET_STATUS` |
| `trigger` | event path `DL_IOC_TRIGGER_EVENT` |
| `clear` | control path `DL_IOC_CLEAR_BUFFER` |
| `poll <timeout-ms>` | event path `.poll` |
| `mmap-read` | shared memory path `.mmap` |

### handle 與 open flags

```c
struct dl_runtime_handle handle = { .fd = -1 };
```

CLI 先把 fd 設成無效狀態。後面 `dl_runtime_open_flags()` 成功後，handle 才持有有效 fd。

如果 subcommand 是 `poll`，CLI 會加 `O_NONBLOCK`。這避免某些 path 在沒有資料時卡住，也讓 poll test 更明確。

### open device

所有 subcommand 都先開同一個 device node：

```c
dl_runtime_open_flags(&handle, argv[1], open_flags)
```

差異不在 device path，而是在後面呼叫哪個 runtime helper。

### `write`

```c
dl_runtime_write(&handle, argv[3], strlen(argv[3]))
```

這會進 driver 的 `.write` callback。成功時 CLI 印 `wrote N bytes`。

### `ioctl-write`

```c
dl_runtime_ioctl_set_message(&handle, argv[3])
```

這會讓 runtime 把字串包成 `struct dl_ioctl_message`，再送 `DL_IOC_SET_MESSAGE` 給 driver。

### `read`

```c
ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1);
buffer[ret] = '\0';
```

CLI 把讀回來的 bytes 補上 `'\0'`，再用 `%s` 印出。這是 userspace 字串處理，不代表 driver 回傳一定自帶 NUL terminator。

### `status`

`status` 呼叫 `dl_runtime_ioctl_get_status()`，再印出：

- `buffer_len`
- `event_count`
- `event_pending`
- `mmap_size`

這是觀察 driver 內部狀態的 control path。

### `trigger` / `poll`

`trigger` 送 `DL_IOC_TRIGGER_EVENT`，讓 driver 設 pending event 並喚醒 waitqueue。

`poll` 則呼叫：

```c
dl_runtime_poll_readable(&handle, timeout_ms, &revents)
```

輸出 `poll ret=... revents=...`。`test.sh` 會 grep `poll ret=1`，確認真的被事件喚醒。

### `mmap-read`

`mmap-read` 呼叫 `dl_runtime_mmap_shared()`，把回傳 pointer 轉成：

```c
struct dl_shared_page *shared;
```

然後印出 magic/version/event/buffer 欄位。讀完後必須呼叫 `dl_runtime_munmap_shared()`。

### close path

不管哪個 subcommand，成功路徑最後都會 `dl_runtime_close(&handle)`。錯誤路徑也會盡量先 close 再 return。

## 關鍵 API / 參數角色

| API | 參數角色 | 在 CLI 中的意義 |
|---|---|---|
| `strcmp(argv[2], "write")` | subcommand 字串比較 | 決定要走哪條 driver path |
| `dl_runtime_open_flags(&handle, argv[1], open_flags)` | runtime handle、device path、flags | 開啟 `/dev/...` |
| `dl_runtime_write(&handle, argv[3], strlen(argv[3]))` | handle、message buffer、長度 | 送 data path write |
| `dl_runtime_ioctl_get_status(&handle, &status)` | handle、status output struct | 讀 driver status |
| `dl_runtime_poll_readable(&handle, timeout_ms, &revents)` | handle、timeout、event mask output | 等待 driver event |
| `dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES)` | handle、mapping 長度 | 取得 shared page mapping |

## 常見卡點

- 第一個參數是 device path，第二個參數才是 subcommand。
- `read` 補 `buffer[ret] = '\0'` 是 CLI 為了印字串，不是 syscall 自動幫你做。
- `poll ret=1` 代表有一個 fd 有事件；真正事件內容要看 `revents` bitmask。
- `mmap-read` 讀的是 shared page snapshot，不會觸發 driver `.read`。
- 每個錯誤 path 都要 close fd，否則測試反覆跑時容易留下資源誤判。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| CLI 的哪個 subcommand 會走 driver `.write`？ | `write <message>`。 |
| `ioctl-write` 直接呼叫 `ioctl()` 嗎？ | 不是，它呼叫 runtime 的 `dl_runtime_ioctl_set_message()`。 |
| `mmap-read` 為什麼能印出 `magic`？ | 它把 mmap 回傳 pointer 轉成 `struct dl_shared_page *` 讀欄位。 |
| Lab03 smoke test 用哪個輸出確認 poll 成功？ | `poll ret=1`。 |
