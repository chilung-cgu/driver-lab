# `driver_lab_runtime.h` 詳解

## 這份檔案的角色

這是 userspace runtime 的 public header。它宣告 `dl_runtime_*()` API，讓 CLI 或測試程式可以呼叫 runtime，而不是直接散落 `open/read/write/ioctl/poll/mmap`。

實作在 [`../src/driver_lab_runtime.c.md`](../src/driver_lab_runtime.c.md)。ABI struct 和 ioctl command 來自 [`driver_lab_uapi.h.md`](driver_lab_uapi.h.md)。

## 先讀哪裡

第一次照 API 分組讀：

1. `struct dl_runtime_handle`
2. open/close helpers
3. read/write data path helpers
4. ioctl control/event helpers
5. poll event helper
6. mmap shared memory helpers

這份 header 的重點不是 C 語法，而是看 runtime 對外承諾哪些能力。

## 主線資料流

```text
CLI include driver_lab_runtime.h
  -> 呼叫 dl_runtime_* API
  -> driver_lab_runtime.c 呼叫 syscall
  -> kernel driver callback
```

`driver_lab_runtime.h` 同時 include `driver_lab_uapi.h`，所以使用者只 include runtime header 就能拿到 ioctl status/shared page 等 ABI type。

## 分區詳解

### include guard

```c
#ifndef DRIVER_LAB_RUNTIME_H
#define DRIVER_LAB_RUNTIME_H
```

避免同一個 translation unit 重複 include 時造成重複宣告。

### standard headers

```c
#include <stddef.h>
#include <sys/types.h>
```

`stddef.h` 提供 `size_t`，`sys/types.h` 提供 `ssize_t`。runtime 的 read/write API 需要這兩種型別。

### UAPI include

```c
#include "driver_lab_uapi.h"
```

這讓 runtime API 可以直接使用：

- `struct dl_ioctl_status`
- `struct dl_shared_page`
- `DL_MMAP_BYTES`

同時也代表 runtime header 和 kernel/userspace 共用 ABI 有明確依賴。

### C++ guard

```c
#ifdef __cplusplus
extern "C" {
#endif
```

這讓 C++ 程式 include 這份 header 時，不會因 C++ name mangling 找不到 C 實作符號。現在 repo 的 CLI 是 C，但這是常見 public C header 寫法。

### `struct dl_runtime_handle`

```c
struct dl_runtime_handle {
    int fd;
};
```

這是 runtime 的最小狀態。第一輪可以把它想成「已開啟的 `/dev/...`」。目前它只包 fd，未來如果 runtime 要加 timeout policy、device metadata 或 error context，可以從這裡擴充。

### data path API

```c
ssize_t dl_runtime_write(...);
ssize_t dl_runtime_read(...);
```

這兩個 helper 對應 driver 的 `.write` / `.read` callback。它們不解讀 payload，只搬 bytes。

### control/event API

```c
int dl_runtime_ioctl_set_message(...);
int dl_runtime_ioctl_get_status(...);
int dl_runtime_ioctl_trigger_event(...);
int dl_runtime_ioctl_clear_buffer(...);
```

這組 API 對應 Lab03 的 `DL_IOC_*` command。`set_message` 和 `get_status` 有 payload struct；`trigger_event` 和 `clear_buffer` 是純 command。

### poll / mmap API

```c
int dl_runtime_poll_readable(...);
void *dl_runtime_mmap_shared(...);
int dl_runtime_munmap_shared(...);
```

`poll` 負責等可讀資料或 driver event。`mmap_shared` 和 `munmap_shared` 負責 shared page mapping 的生命週期。

## API 分組速查

| API | 對應 syscall | 對應 driver path |
|---|---|---|
| `dl_runtime_open*` / `dl_runtime_close` | `open` / `close` | open/release |
| `dl_runtime_write` / `dl_runtime_read` | `write` / `read` | data path |
| `dl_runtime_ioctl_*` | `ioctl` | control/event path |
| `dl_runtime_poll_readable` | `poll` | event path |
| `dl_runtime_mmap_shared` / `dl_runtime_munmap_shared` | `mmap` / `munmap` | shared memory path |

## 常見卡點

- 這份 header 只是宣告，不包含實作。
- `struct dl_runtime_handle` 目前只有 fd，不代表 kernel 端也有同名物件。
- include runtime header 會間接 include UAPI header；如果 UAPI 改了，runtime caller 也可能受影響。
- `dl_runtime_mmap_shared()` 回傳 `void *`，caller 要自己轉成正確的 shared page struct。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| runtime handle 目前保存什麼？ | 一個 userspace fd。 |
| 哪些 API 對應 ioctl？ | `dl_runtime_ioctl_set_message`、`get_status`、`trigger_event`、`clear_buffer`。 |
| 為什麼這份 header include `driver_lab_uapi.h`？ | 因為 runtime API 會暴露 UAPI struct/type。 |
| C++ guard 的目的？ | 讓 C++ caller 能用 C linkage 呼叫 runtime function。 |
