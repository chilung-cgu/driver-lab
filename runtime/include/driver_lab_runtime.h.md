# `driver_lab_runtime.h` 詳解

## 結論

`runtime/include/driver_lab_runtime.h` 是 userspace runtime 的 public API。它告訴 CLI 和測試程式：

```text
你可以用哪些 dl_runtime_*() helper 操作 lab driver
每個 helper 需要什麼參數
哪些 UAPI struct/type 會暴露給 caller
```

實作在 [`../src/driver_lab_runtime.c.md`](../src/driver_lab_runtime.c.md)，共用 ABI 在 [`driver_lab_uapi.h.md`](driver_lab_uapi.h.md)。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`driver_lab_runtime.h`](driver_lab_runtime.h) 本身。
- runtime 實作：[`../src/driver_lab_runtime.c.md`](../src/driver_lab_runtime.c.md)。
- runtime caller：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)。
- UAPI header：[`driver_lab_uapi.h.md`](driver_lab_uapi.h.md)。

這份 header 目前只是一個教學 runtime API，不保證是穩定產品級 library ABI。

## 先理解這份檔案在 repo 的位置

userspace 端的包含關係：

```text
tests/driver_lab_char_cli.c
  -> #include "driver_lab_runtime.h"
      -> #include "driver_lab_uapi.h"
```

所以 CLI include runtime header 後，同時得到：

- `struct dl_runtime_handle`
- `dl_runtime_*()` function declaration
- `struct dl_ioctl_status`
- `struct dl_shared_page`
- `DL_MMAP_BYTES`

## 這份檔案要解決什麼問題？

如果沒有這份 header，CLI 只能：

- 自己宣告 runtime function prototype。
- 或直接 include UAPI 並散寫 syscall。

有了這份 header，CLI 可以只面對 runtime API：

```c
struct dl_runtime_handle handle = { .fd = -1 };
dl_runtime_open_flags(&handle, argv[1], open_flags);
dl_runtime_ioctl_get_status(&handle, &status);
dl_runtime_close(&handle);
```

白話講：

```text
runtime.h 是 userspace caller 和 runtime.c 之間的契約
uapi.h 是 userspace runtime/CLI 和 kernel driver 之間的契約
```

## 一、include guard

原始碼：

```c
#ifndef DRIVER_LAB_RUNTIME_H
#define DRIVER_LAB_RUNTIME_H
...
#endif
```

避免同一個 C 檔重複 include 造成重複宣告。

## 二、standard headers

原始碼：

```c
#include <stddef.h>
#include <sys/types.h>
```

這兩個 header 提供 runtime API 需要的 type：

| type | 來源 | 用途 |
|---|---|---|
| `size_t` | `<stddef.h>` | buffer length / mmap length |
| `ssize_t` | `<sys/types.h>` | read/write 回傳 bytes 或 `-1` |

## 三、include UAPI

原始碼：

```c
#include "driver_lab_uapi.h"
```

這是關鍵設計。runtime API 直接使用 UAPI type：

```c
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
                                struct dl_ioctl_status *status);
```

如果 runtime header 不 include UAPI，caller 就需要額外 include `driver_lab_uapi.h` 才能看到 `struct dl_ioctl_status`。

白話講：

```text
include runtime.h
就足夠寫 Lab03 CLI
```

## 四、C++ linkage guard

原始碼：

```c
#ifdef __cplusplus
extern "C" {
#endif
...
#ifdef __cplusplus
}
#endif
```

這讓 C++ 程式 include 這份 C header 時，function name 維持 C linkage，不被 C++ name mangling 改名。

目前 repo 的 CLI 是 C，不是 C++。但這種 guard 是 public C header 常見寫法。

## 五、`struct dl_runtime_handle`

原始碼：

```c
struct dl_runtime_handle {
	int fd;
};
```

目前 handle 只保存一個 fd。

這個 fd 是 userspace process 的 file descriptor number，不是 kernel `struct file *`。kernel 端 file object 由 kernel 管，runtime 只保存 userspace 這邊的參照。

CLI 初始化：

```c
struct dl_runtime_handle handle = { .fd = -1 };
```

runtime open 成功後：

```text
handle.fd = open("/dev/driver_lab_ctl0", flags)
```

runtime close 成功後：

```text
handle.fd = -1
```

## 六、open / close API

原始碼：

```c
int dl_runtime_open(struct dl_runtime_handle *handle, const char *path);
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags);
int dl_runtime_close(struct dl_runtime_handle *handle);
```

差異：

| API | 用途 |
|---|---|
| `dl_runtime_open()` | 預設用 `O_RDWR` 打開 device |
| `dl_runtime_open_flags()` | caller 自己提供 flags，例如 `O_NONBLOCK` |
| `dl_runtime_close()` | 關閉 fd 並把 handle 重設為 invalid |

## 七、data path API

原始碼：

```c
ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count);
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count);
```

這兩個 helper 對應 driver 的 `.write` / `.read` callback。

它們不解讀 payload，只搬 bytes：

```text
runtime write/read
  -> syscall write/read
  -> driver dl_write/dl_read
```

## 八、control / event ioctl API

原始碼：

```c
int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message);
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle, struct dl_ioctl_status *status);
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle);
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle);
```

分工：

| API | 對應 command | payload |
|---|---|---|
| `set_message` | `DL_IOC_SET_MESSAGE` | `struct dl_ioctl_message`，由 runtime 內部建立 |
| `get_status` | `DL_IOC_GET_STATUS` | caller 提供 `struct dl_ioctl_status *` |
| `trigger_event` | `DL_IOC_TRIGGER_EVENT` | 無 payload |
| `clear_buffer` | `DL_IOC_CLEAR_BUFFER` | 無 payload |

`set_message` 的參數是 `const char *message`，不是 `struct dl_ioctl_message *`。這表示 runtime API 有意把 UAPI payload 包裝起來，讓 CLI 不必自己建 struct。

## 九、poll / mmap API

原始碼：

```c
int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms, short *revents);
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length);
int dl_runtime_munmap_shared(void *addr, size_t length);
```

分工：

| API | 對應 syscall | 重點 |
|---|---|---|
| `dl_runtime_poll_readable()` | `poll()` | 等 `POLLIN | POLLPRI`，把 event mask 回填到 `revents` |
| `dl_runtime_mmap_shared()` | `mmap()` | 映射 driver shared page |
| `dl_runtime_munmap_shared()` | `munmap()` | 解除 mapping |

注意 `mmap_shared()` 回傳 `void *`。caller 要依 UAPI layout 轉型：

```c
struct dl_shared_page *shared;
shared = dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
```

## API 分組速查

| API | 對應 driver path |
|---|---|
| `dl_runtime_open*` / `close` | `.open` / `.release` |
| `dl_runtime_write` / `read` | data path |
| `dl_runtime_ioctl_*` | control/event path |
| `dl_runtime_poll_readable` | event path |
| `dl_runtime_mmap_shared` / `munmap_shared` | shared memory path |

## 常見卡點

- 這份 header 只有宣告，實作在 `driver_lab_runtime.c`。
- `struct dl_runtime_handle` 是 userspace fd wrapper，不是 kernel object。
- include runtime header 會間接 include UAPI header。
- `dl_runtime_ioctl_set_message()` 對外收 C 字串，但內部會轉成 UAPI struct。
- `dl_runtime_mmap_shared()` 回傳 `void *`，caller 必須知道要用 `struct dl_shared_page` 解讀。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `driver_lab_runtime.h` 是給 kernel 還是 userspace include？ | userspace；kernel driver include 的是 UAPI header。 |
| runtime handle 目前保存什麼？ | 一個 userspace fd。 |
| 哪個 API 讓 caller 自己傳 `O_NONBLOCK`？ | `dl_runtime_open_flags()`。 |
| 哪個 API 回傳 `struct dl_ioctl_status`？ | `dl_runtime_ioctl_get_status()`。 |
| `dl_runtime_mmap_shared()` 回傳後要用哪個 struct 解讀？ | `struct dl_shared_page`。 |
