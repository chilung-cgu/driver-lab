# `driver_lab_runtime.c` 詳解

## 結論

`runtime/src/driver_lab_runtime.c` 的角色是：

> 把 userspace 會用到的 `open / close / read / write / ioctl / poll / mmap / munmap` 包成一組 `dl_runtime_*()` API，讓 CLI 和測試程式不用每次都直接處理 fd、UAPI struct、ioctl command、poll event mask、mmap 參數與錯誤回傳。

這份檔案站在 repo 的 userspace 這一側：

```text
kernel driver 定義 ABI 與 file_operations
        ↓
runtime 包裝 ABI / syscall
        ↓
CLI / smoke test 呼叫 runtime
        ↓
userspace 操作 /dev/... device node
```

第一輪讀它時，請先不要把它想成「一個產品級 library」。目前它比較像：

> 一份教學用 userspace helper source，直接和 CLI 一起編譯，幫你看清楚 driver 不只是一個 `.ko`，還包含 userspace 怎麼打開 device、送 command、等事件、讀 shared page。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- repo 內的 [`runtime/src/driver_lab_runtime.c`](driver_lab_runtime.c)、[`../include/driver_lab_runtime.h`](../include/driver_lab_runtime.h)、[`../include/driver_lab_uapi.h`](../include/driver_lab_uapi.h)、[`../Makefile`](../Makefile)、[`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c)、[`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c)。
- `open()`、`poll()`、`mmap()` / `munmap()` 的 userspace 語意以 Linux man-pages 6.18 為準。
- `_IO/_IOW/_IOR` 的方向語意以 Linux kernel documentation 的 ioctl-number 文件為準。

沒有在這份文件中展開所有 kernel VFS、page fault、或 glibc wrapper 實作細節；以下只聚焦「讀懂這份 runtime.c 必要的上下文」。

## 先理解 runtime 在整個 repo 的位置

`runtime/` 目前有三個主檔：

```text
runtime/include/driver_lab_runtime.h
runtime/include/driver_lab_uapi.h
runtime/src/driver_lab_runtime.c
```

你可以這樣分：

| 檔案 | 角色 | 第一輪怎麼看 |
|---|---|---|
| [`../include/driver_lab_uapi.h`](../include/driver_lab_uapi.h) | kernel/userspace 共用 ABI | 定義 ioctl command、payload struct、mmap shared page layout |
| [`../include/driver_lab_runtime.h`](../include/driver_lab_runtime.h) | runtime 對外 API | 宣告 `dl_runtime_*()` |
| [`driver_lab_runtime.c`](driver_lab_runtime.c) | runtime 實作 | 把 syscall 和 UAPI struct 包成 helper |

這份 runtime 主要服務：

- [`../../labs/02-char-device`](../../labs/02-char-device)：`read/write`
- [`../../labs/03-ioctl-poll-mmap`](../../labs/03-ioctl-poll-mmap)：`read/write/ioctl/poll/mmap`

也就是說，你不要把 `runtime.c` 想成 kernel driver。它是 userspace 端的一層薄包裝，最後還是要透過 device node 進入 kernel driver callback。

## `runtime.c` 要解決什麼問題？

檔案開頭註解已經講出核心：

```c
/*
 * runtime 的目的不是取代 driver，而是把 scattered syscall 包成一致 API。
 * 這讓 CLI / sample app 不需要每次都自己處理 open/read/ioctl/poll/mmap 細節。
 */
```

白話講，如果沒有 runtime，CLI 會到處寫：

```c
open(...);
write(...);
read(...);
ioctl(...);
poll(...);
mmap(...);
munmap(...);
close(...);
```

而且每個地方都要自己記得：

- fd 怎麼初始化、失敗怎麼判斷。
- userspace 失敗慣例是 `-1` 並設定 `errno`。
- `DL_IOC_SET_MESSAGE` 要傳 `struct dl_ioctl_message`，不是直接傳 `char *`。
- `poll()` 要填 `struct pollfd.events`，結果看 `revents`。
- `mmap()` 失敗不是回 `NULL`，而是回 `MAP_FAILED`。

有了 runtime，CLI 可以寫成：

```c
dl_runtime_open_flags(&handle, path, flags);
dl_runtime_ioctl_set_message(&handle, "hello");
dl_runtime_poll_readable(&handle, 3000, &revents);
dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
dl_runtime_close(&handle);
```

這就是「包裝層」：它不改變 driver ABI，但讓 userspace caller 比較不容易把 ABI 細節寫散。

## 先看 Makefile：`runtime.c` 怎麼被用到？

[`runtime/Makefile`](../Makefile) 目前不是把 runtime 編成 `.a` 或 `.so`，而是直接把 runtime source 和 CLI source link 成一支可執行檔：

```make
CLI := ../tests/driver_lab_char_cli

$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
```

等價概念是：

```sh
cc -Wall -Wextra -Werror -std=c11 -Iinclude \
  -o ../tests/driver_lab_char_cli \
  src/driver_lab_runtime.c \
  ../tests/driver_lab_char_cli.c
```

所以目前的架構是：

```text
driver_lab_runtime.c 不是獨立 library artifact
而是被直接編進 driver_lab_char_cli
```

這個設計對學習很有用，因為你可以直接從 CLI trace 到 runtime，再 trace 到 kernel driver：

```text
tests/driver_lab_char_cli.c
  -> dl_runtime_ioctl_set_message()
  -> ioctl(fd, DL_IOC_SET_MESSAGE, &msg)
  -> labs/03 driver 的 dl_unlocked_ioctl()
```

## 讀 `runtime.c` 的主線

整份檔案可以分成 6 區：

| 區塊 | function | 對應 syscall / 概念 |
|---|---|---|
| 開關 device | `dl_runtime_open()` / `dl_runtime_open_flags()` / `dl_runtime_close()` | `open()` / `close()` |
| data path | `dl_runtime_write()` / `dl_runtime_read()` | `write()` / `read()` |
| control path | `dl_runtime_ioctl_set_message()` / `dl_runtime_ioctl_get_status()` / `dl_runtime_ioctl_clear_buffer()` | `ioctl()` |
| event path | `dl_runtime_ioctl_trigger_event()` / `dl_runtime_poll_readable()` | `ioctl()` + `poll()` |
| shared memory path | `dl_runtime_mmap_shared()` | `mmap()` |
| 解除 mapping | `dl_runtime_munmap_shared()` | `munmap()` |

讀的時候請一直問兩個問題：

1. 這個 helper 是在替 CLI 隱藏哪個 syscall 細節？
2. 這個 syscall 進到 Lab03 driver 後會對應哪個 callback 或 command？

## 一、include 區

原始碼：

```c
#include "driver_lab_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
```

這些 header 分工如下：

| header | 用途 |
|---|---|
| `driver_lab_runtime.h` | 宣告 `dl_runtime_*()` API，並 include `driver_lab_uapi.h` |
| `<errno.h>` | 設定 `errno = EINVAL / EMSGSIZE` |
| `<fcntl.h>` | `O_RDWR` 等 open flags |
| `<poll.h>` | `struct pollfd`、`POLLIN`、`POLLPRI` |
| `<string.h>` | `strlen()`、`memset()`、`memcpy()` |
| `<sys/ioctl.h>` | `ioctl()` |
| `<sys/mman.h>` | `mmap()`、`munmap()`、`MAP_FAILED` |
| `<unistd.h>` | `open()`、`read()`、`write()`、`close()` |

最重要的是：

```c
#include "driver_lab_runtime.h"
```

因為 [`driver_lab_runtime.h`](../include/driver_lab_runtime.h) 又 include [`driver_lab_uapi.h`](../include/driver_lab_uapi.h)，所以 `runtime.c` 才能看到：

```c
struct dl_ioctl_message;
struct dl_ioctl_status;
struct dl_shared_page;
DL_IOC_SET_MESSAGE;
DL_IOC_GET_STATUS;
DL_IOC_TRIGGER_EVENT;
DL_IOC_CLEAR_BUFFER;
DL_MMAP_BYTES;
```

白話講：

```text
runtime.c include runtime.h
runtime.h include uapi.h
所以 runtime.c 同時知道「自己的 API」和「driver ABI」
```

## 二、`dl_runtime_open()`：預設用 `O_RDWR` 打開 device

原始碼：

```c
int dl_runtime_open(struct dl_runtime_handle *handle, const char *path)
{
	return dl_runtime_open_flags(handle, path, O_RDWR);
}
```

這個 function 很薄，只是幫 caller 預設用 `O_RDWR`：

```text
dl_runtime_open(handle, path)
    ↓
dl_runtime_open_flags(handle, path, O_RDWR)
```

為什麼預設是 `O_RDWR`？

- Lab02/Lab03 的 device node 通常同時需要讀和寫。
- Lab03 的 `mmap()` helper 用的是 `PROT_READ | PROT_WRITE` 和 `MAP_SHARED`；Linux `mmap(2)` 文件也明確列出，若 `MAP_SHARED` 搭配 `PROT_WRITE`，fd 沒有用 read/write 開啟可能造成 `EACCES`。

白話講：

```text
大部分 lab 操作都需要同一個 fd 能讀也能寫
所以 default open 先給 O_RDWR
特殊情況再用 open_flags 版本補 O_NONBLOCK
```

對應 CLI：

```c
int open_flags = O_RDWR;

if (strcmp(argv[2], "poll") == 0)
	open_flags |= O_NONBLOCK;

dl_runtime_open_flags(&handle, argv[1], open_flags);
```

## 三、`dl_runtime_open_flags()`：真正的開檔入口

原始碼：

```c
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags)
{
	if (!handle || !path) {
		errno = EINVAL;
		return -1;
	}

	handle->fd = open(path, flags);
	if (handle->fd < 0)
		return -1;

	return 0;
}
```

這是 runtime 的真正開檔入口。它做三件事：

1. 檢查 `handle` 和 `path` 不能是 `NULL`。
2. 呼叫 `open(path, flags)`。
3. 成功後把 fd 存到 `handle->fd`。

`open()` 成功時會回傳一個非負整數 fd；失敗時回 `-1` 並設定 `errno`。所以這段：

```c
handle->fd = open(path, flags);
if (handle->fd < 0)
	return -1;
```

是在沿用 userspace C library 的錯誤慣例，不是 kernel 內部的負 errno 慣例。

白話講：

```text
把 /dev/driver_lab_ctl0 打開
把拿到的 fd 存到 handle
後面所有 runtime helper 都靠 handle->fd 做事
```

成功後的 `handle` 可以想成：

```text
struct dl_runtime_handle
  fd = 3   // 例子：目前 process 裡的一個 open device fd
```

常見誤解：

- `handle->fd` 不是 kernel 裡的 `struct file *`。
- 它只是 userspace process 看到的 fd number。
- 真正的 kernel file object 由 kernel 管，userspace 只能透過 fd 參照它。

## 四、`dl_runtime_close()`：關 fd，並避免 caller 誤用舊 fd

原始碼：

```c
int dl_runtime_close(struct dl_runtime_handle *handle)
{
	int ret;

	if (!handle) {
		errno = EINVAL;
		return -1;
	}

	if (handle->fd < 0)
		return 0;

	ret = close(handle->fd);
	if (ret < 0)
		return -1;

	handle->fd = -1;
	return 0;
}
```

這段有兩個新手容易忽略的細節。

第一，`fd < 0` 時直接回成功：

```c
if (handle->fd < 0)
	return 0;
```

這讓 `close` helper 對「已經無效的 handle」比較寬容。CLI error path 可以放心呼叫 close，不必每次先判斷 fd 是否有效。

第二，`close()` 成功後把 fd 設回 `-1`：

```c
handle->fd = -1;
```

這是防止 caller 後續誤用已關閉 fd。fd number 是 process-local 的小整數；關掉後，作業系統未來可能把同一個 number 分配給別的檔案。如果 runtime 不把它標成 invalid，caller 可能以為自己還拿著舊 device。

白話講：

```text
close 成功後，這個 handle 就不再代表任何 /dev node
所以 fd 要重設成 -1
```

## 五、`dl_runtime_write()`：data path 的寫入 helper

原始碼：

```c
ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}

	return write(handle->fd, buf, count);
}
```

它做的事情很少：

1. 檢查 handle 有效。
2. 檢查 fd 有效。
3. 檢查 buffer 不為 `NULL`。
4. 呼叫 `write()`。

它不解讀 `buf` 裡面的內容。這很重要。

runtime 不知道這段 bytes 對 driver 是 command、文字、binary blob，還是其他格式；它只是把 bytes 交給 fd。真正語意由 driver 的 `.write` callback 決定。

在 Lab03，這個 syscall 會走到：

```text
dl_runtime_write()
  -> write(fd, buf, count)
  -> Lab03 driver 的 dl_write()
  -> dl_publish_message_locked()
  -> 更新 dl_buffer / event_count / shared page
```

白話講：

```text
runtime_write 只是 userspace 端的搬運工
driver 的 dl_write 才決定「寫進來的 bytes 代表什麼」
```

## 六、`dl_runtime_read()`：data path 的讀取 helper

原始碼：

```c
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}

	return read(handle->fd, buf, count);
}
```

它和 write helper 對稱：

```text
dl_runtime_read()
  -> read(fd, buf, count)
  -> Lab03 driver 的 dl_read()
  -> simple_read_from_buffer()
```

runtime 這裡也不解讀 driver 回傳的 bytes。CLI 讀完後才把它補成 C string：

```c
ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1);
buffer[ret] = '\0';
printf("read %zd bytes: %s\n", ret, buffer);
```

注意：`buffer[ret] = '\0'` 是 CLI 為了印 `%s` 做的 userspace 字串處理，不是 `read()` 自動幫你加結尾。

白話講：

```text
read syscall 只保證回傳 bytes 數
要不要把 bytes 當 C 字串，是 CLI 自己的決定
```

## 七、`dl_runtime_ioctl_set_message()`：把 C 字串包成 UAPI struct

這是整份 runtime 最值得慢慢讀的 function。

原始碼：

```c
int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message)
{
	struct dl_ioctl_message msg;
	size_t len;

	if (!handle || handle->fd < 0 || !message) {
		errno = EINVAL;
		return -1;
	}

	memset(&msg, 0, sizeof(msg));
	len = strlen(message);
	if (len >= sizeof(msg.text)) {
		errno = EMSGSIZE;
		return -1;
	}

	memcpy(msg.text, message, len);
	return ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg);
}
```

CLI 傳進來的是：

```text
"hello-ioctl"
```

但 Lab03 UAPI 規定 `DL_IOC_SET_MESSAGE` 的 payload 是：

```c
struct dl_ioctl_message {
	char text[DL_MESSAGE_BYTES];
};
```

所以 runtime 必須把 C 字串包進 ABI struct：

```text
char *message
  -> struct dl_ioctl_message msg
  -> ioctl(fd, DL_IOC_SET_MESSAGE, &msg)
```

### 先清空 struct

```c
memset(&msg, 0, sizeof(msg));
```

這讓 `msg.text` 未使用的部分都是 0。對固定大小 ABI struct 來說，這是乾淨的習慣，避免把 stack 上的舊資料帶進 ioctl payload。

### 檢查長度

```c
len = strlen(message);
if (len >= sizeof(msg.text)) {
	errno = EMSGSIZE;
	return -1;
}
```

`msg.text` 是固定大小 array。這裡用 `>=`，是因為 runtime 想保留至少一個 NUL byte 的空間；即使後面 `memcpy()` 只複製 `len` bytes，前面的 `memset()` 已經把剩下空間清成 0。

白話講：

```text
如果 message 長到塞滿整個 text[]
就沒有空間留下字串結尾的 0
所以 runtime 直接拒絕
```

錯誤用 `EMSGSIZE`，意思是 message 太大，不符合這個 ABI 的固定大小限制。

### 複製到 payload 並送 ioctl

```c
memcpy(msg.text, message, len);
return ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg);
```

這裡傳給 driver 的不是原始 `char *message`，而是 `&msg`。

對應到 Lab03 driver：

```text
runtime:
  ioctl(fd, DL_IOC_SET_MESSAGE, &msg)

kernel driver:
  copy_from_user(&msg, (void __user *)arg, sizeof(msg))
```

`DL_IOC_SET_MESSAGE` 在 UAPI 裡用 `_IOW` 定義。Linux kernel documentation 對 `_IOW` 的語意是從 userspace 觀點看「write」，也就是 userspace 寫資料給 kernel；kernel driver 實作時會從 userspace read/copy 進 kernel。

白話講：

```text
CLI 給 runtime 一個字串
runtime 把它裝進 ABI struct
driver 再從 userspace pointer copy 進 kernel
```

這就是 runtime 包裝 ABI 的典型例子。

## 八、`dl_runtime_ioctl_get_status()`：讀回 driver 狀態

原始碼：

```c
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
								struct dl_ioctl_status *status)
{
	if (!handle || handle->fd < 0 || !status) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_GET_STATUS, status);
}
```

這個 helper 的 caller 會準備一個 output struct：

```c
struct dl_ioctl_status status;
dl_runtime_ioctl_get_status(&handle, &status);
```

driver 收到 `DL_IOC_GET_STATUS` 後，會填好：

```c
status.buffer_len = dl_buffer_len;
status.event_count = dl_event_count;
status.event_pending = dl_event_pending ? 1U : 0U;
status.mmap_size = DL_MMAP_BYTES;
```

再用 `copy_to_user()` 回傳給 userspace。

UAPI 裡 `DL_IOC_GET_STATUS` 用 `_IOR` 定義。方向一樣是 userspace 觀點：userspace read from kernel；kernel driver 實作時會 write/copy to userspace。

白話講：

```text
runtime 把 status struct 的地址交給 driver
driver 把目前狀態填回那塊 userspace memory
CLI 再把 status 印出來
```

CLI 輸出長這樣：

```text
buffer_len=11 event_count=1 event_pending=1 mmap_size=4096
```

## 九、`dl_runtime_ioctl_trigger_event()`：產生 pending event

原始碼：

```c
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_TRIGGER_EVENT);
}
```

這個 command 沒有 payload，所以 `ioctl()` 只帶 fd 和 command：

```c
ioctl(handle->fd, DL_IOC_TRIGGER_EVENT);
```

對應到 Lab03 driver，`DL_IOC_TRIGGER_EVENT` 會：

```text
dl_event_count++
dl_event_pending = true
dl_sync_shared_page_locked()
wake_up_interruptible(&dl_event_wq)
```

它的主要用途是配合 `poll()` 測試：

```text
terminal/test:
  先跑 poll 3000 等事件
  再跑 trigger
  poll 被 driver 喚醒
```

白話講：

```text
trigger 不寫 message
它只是叫 driver 標記「有事件了」
並喚醒正在 poll 的 userspace
```

## 十、`dl_runtime_ioctl_clear_buffer()`：清掉 buffer 與 event state

原始碼：

```c
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_CLEAR_BUFFER);
}
```

這也是無 payload command。

對應到 Lab03 driver，`DL_IOC_CLEAR_BUFFER` 會：

```text
memset(dl_buffer, 0, sizeof(dl_buffer))
dl_buffer_len = 0
dl_event_pending = false
dl_sync_shared_page_locked()
wake_up_interruptible(&dl_event_wq)
```

白話講：

```text
clear 是 control path
目的不是讀寫資料
而是把 driver 狀態清回乾淨狀態
```

這在 smoke test 末段很有用，避免下一次測試繼承上一次的 buffer/event 狀態。

## 十一、`dl_runtime_poll_readable()`：等待可讀資料或 driver event

原始碼：

```c
int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms,
							 short *revents)
{
	struct pollfd pfd;
	int ret;

	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	pfd.fd = handle->fd;
	pfd.events = POLLIN | POLLPRI;
	pfd.revents = 0;

	ret = poll(&pfd, 1, timeout_ms);
	if (ret >= 0 && revents)
		*revents = pfd.revents;

	return ret;
}
```

先看 `struct pollfd` 的三個欄位：

```c
struct pollfd {
	int   fd;
	short events;
	short revents;
};
```

在這份 runtime 中：

```c
pfd.fd = handle->fd;
pfd.events = POLLIN | POLLPRI;
pfd.revents = 0;
```

意思是：

| 欄位 | 方向 | Lab03 中的意思 |
|---|---|---|
| `fd` | input | 要監看的 `/dev/driver_lab_ctl0` fd |
| `events` | input | userspace 感興趣的事件 |
| `revents` | output | kernel 實際回報發生的事件 |

`POLLIN` 是「有資料可讀」。`POLLPRI` 在一般 Linux 語意上代表 exceptional condition；在這個 lab 中，driver 用它表示 `event_pending`。

呼叫：

```c
ret = poll(&pfd, 1, timeout_ms);
```

第二個參數 `1` 表示這裡只監看一個 fd。`timeout_ms` 是最多等幾毫秒；`poll()` 回傳：

| 回傳值 | 意義 |
|---|---|
| `> 0` | 有幾個 fd 的 `revents` 非 0 |
| `0` | timeout |
| `-1` | 失敗，並設定 `errno` |

所以這段：

```c
if (ret >= 0 && revents)
	*revents = pfd.revents;
```

代表只要 `poll()` 沒失敗，就把實際 event mask 回填給 caller。

白話講：

```text
runtime 問 kernel：
這個 fd 現在可讀嗎？
或有 driver event 嗎？

kernel 回答：
有/沒有/timeout/錯誤
並把細節放在 revents
```

對應到 Lab03 driver：

```text
dl_runtime_poll_readable()
  -> poll(fd)
  -> driver 的 dl_poll()
  -> poll_wait(file, &dl_read_wq, wait)
  -> poll_wait(file, &dl_event_wq, wait)
  -> 若 dl_buffer_len > 0 回 POLLIN
  -> 若 dl_event_pending 回 POLLPRI
```

常見誤解：

- `poll()` 的回傳值不是 event mask。
- event mask 在 `pfd.revents`。
- `O_NONBLOCK` 不會讓 `poll()` 自己變成「不等待」；`poll()` 是否等待主要看 `timeout_ms`。這份 CLI 對 `poll` subcommand 加 `O_NONBLOCK`，比較像是避免同一個 fd 後續其他操作被 blocking 語意影響。

## 十二、`dl_runtime_mmap_shared()`：映射 driver shared page

原始碼：

```c
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length)
{
	if (!handle || handle->fd < 0 || length == 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	return mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);
}
```

這個 helper 先檢查：

- `handle` 不能是 `NULL`。
- `fd` 必須有效。
- `length` 不能是 0。

`length == 0` 直接拒絕是正確的：Linux `mmap()` 自 Linux 2.6.12 起，長度為 0 會因 `EINVAL` 失敗。

真正的 mapping 呼叫是：

```c
mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);
```

逐參數看：

| 參數 | 這裡的值 | 意義 |
|---|---|---|
| `addr` | `NULL` | 讓 kernel 選 userspace virtual address |
| `length` | caller 傳入，通常是 `DL_MMAP_BYTES` | 要映射的大小 |
| `prot` | `PROT_READ | PROT_WRITE` | userspace 想讀寫這段 mapping |
| `flags` | `MAP_SHARED` | shared mapping |
| `fd` | `handle->fd` | 對應 `/dev/driver_lab_ctl0` |
| `offset` | `0` | 從 driver 支援的 offset 0 開始 |

成功後，`mmap()` 回傳 userspace address。失敗時回 `MAP_FAILED`，不是 `NULL`。

CLI 用法：

```c
shared = dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
if (shared == MAP_FAILED) {
	perror("dl_runtime_mmap_shared");
	...
}

printf("magic=0x%x version=%u ...\n",
       shared->magic, shared->version, ...);
```

對應到 Lab03 driver：

```text
dl_runtime_mmap_shared()
  -> mmap(fd, offset 0)
  -> driver 的 dl_mmap()
  -> remap_pfn_range(...)
  -> userspace 看到 struct dl_shared_page
```

白話講：

```text
runtime 沒有自己 malloc 一份 buffer
它是請 kernel 把 driver 控制的一頁 memory 映射進目前 process
CLI 再用 struct dl_shared_page 的 layout 解讀那頁 memory
```

## 十三、`dl_runtime_munmap_shared()`：解除 shared mapping

原始碼：

```c
int dl_runtime_munmap_shared(void *addr, size_t length)
{
	if (!addr || length == 0) {
		errno = EINVAL;
		return -1;
	}

	return munmap(addr, length);
}
```

這是 `dl_runtime_mmap_shared()` 的收尾 helper。

`munmap()` 成功回 `0`，失敗回 `-1` 並設定 `errno`。Linux `mmap(2)` 文件也提醒：`munmap()` 後，指定 address range 內的後續存取會變成 invalid memory reference。

這裡有一個重要限制：

```text
runtime 只檢查 addr != NULL 和 length != 0
它無法證明 addr 一定是剛剛 dl_runtime_mmap_shared() 回來的 mapping
```

所以 pairing 責任在 caller：

```text
dl_runtime_mmap_shared()
    ↓ 成功
使用 shared page
    ↓
dl_runtime_munmap_shared()
```

白話講：

```text
open 要 close
mmap 要 munmap
這是 userspace resource lifecycle
```

## runtime 和 driver / CLI 的對照

| CLI subcommand | runtime helper | syscall | Lab03 driver path |
|---|---|---|---|
| `write <msg>` | `dl_runtime_write()` | `write()` | `dl_write()` |
| `read` | `dl_runtime_read()` | `read()` | `dl_read()` |
| `ioctl-write <msg>` | `dl_runtime_ioctl_set_message()` | `ioctl(DL_IOC_SET_MESSAGE)` | `dl_unlocked_ioctl()` |
| `status` | `dl_runtime_ioctl_get_status()` | `ioctl(DL_IOC_GET_STATUS)` | `dl_unlocked_ioctl()` |
| `trigger` | `dl_runtime_ioctl_trigger_event()` | `ioctl(DL_IOC_TRIGGER_EVENT)` | `dl_unlocked_ioctl()` |
| `clear` | `dl_runtime_ioctl_clear_buffer()` | `ioctl(DL_IOC_CLEAR_BUFFER)` | `dl_unlocked_ioctl()` |
| `poll <timeout>` | `dl_runtime_poll_readable()` | `poll()` | `dl_poll()` |
| `mmap-read` | `dl_runtime_mmap_shared()` | `mmap()` | `dl_mmap()` |

這張表是讀 runtime 的主地圖。只要你知道 CLI subcommand，就能一路 trace 到 runtime helper，再 trace 到 driver callback。

## 關鍵 API / 參數角色

| API | 參數角色 | 在 runtime 中的意義 |
|---|---|---|
| `open(path, flags)` | device path、open flags | 取得 `/dev/...` fd；成功回非負 fd，失敗回 `-1 + errno` |
| `close(handle->fd)` | fd | 釋放 userspace fd；成功後 runtime 把 fd 設回 `-1` |
| `write(handle->fd, buf, count)` | fd、userspace source、長度 | 進入 driver `.write` |
| `read(handle->fd, buf, count)` | fd、userspace destination、長度 | 進入 driver `.read` |
| `ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg)` | fd、command、userspace payload pointer | 送 UAPI struct 給 driver `.unlocked_ioctl` |
| `poll(&pfd, 1, timeout_ms)` | pollfd array、fd 數量、timeout | 等待 driver 可讀或 event |
| `mmap(NULL, length, PROT_READ \| PROT_WRITE, MAP_SHARED, handle->fd, 0)` | address hint、大小、權限、mapping 類型、fd、offset | 映射 driver shared page |
| `munmap(addr, length)` | mapping 起點、大小 | 解除 userspace mapping |

## 常見卡點

- runtime 是 userspace code，不會直接碰 kernel 的 `struct file_operations`。
- userspace 慣例是失敗回 `-1` 並設定 `errno`；kernel driver callback 常見的是回負 errno，例如 `-EINVAL`。
- `DL_IOC_SET_MESSAGE` 傳給 driver 的不是原始 `char *`，而是 `struct dl_ioctl_message *`。
- `_IOW/_IOR` 的方向是從 userspace 觀點看，不是從 kernel implementation 觀點看。
- `poll()` 回傳值不是 `POLLIN/POLLPRI` mask；mask 在 `revents`。
- `mmap()` 失敗要比對 `MAP_FAILED`，不是比對 `NULL`。
- `mmap()` 成功後即使 fd close，mapping 不會因為 close fd 自動消失；mapping lifecycle 要用 `munmap()` 管。
- `dl_runtime_munmap_shared()` 不能證明 addr 是否正確，caller 要自己配對 mmap/munmap。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `runtime.c` 是 kernel driver 嗎？ | 不是。它是 userspace helper source，透過 syscall 操作 `/dev/...`。 |
| 目前 runtime 會被編成 `.so` 或 `.a` 嗎？ | 不會。`runtime/Makefile` 直接把 `driver_lab_runtime.c` 和 CLI source link 成 `tests/driver_lab_char_cli`。 |
| `dl_runtime_open()` 和 `dl_runtime_open_flags()` 差在哪？ | 前者固定用 `O_RDWR`；後者讓 caller 額外傳入 flags，例如 `O_NONBLOCK`。 |
| `dl_runtime_close()` 成功後為什麼要把 fd 設成 `-1`？ | 避免 caller 誤用已關閉 fd，因為 fd number 未來可能被 OS 重用。 |
| `dl_runtime_write()` 會解析 message 嗎？ | 不會。它只把 bytes 交給 `write()`，語意由 driver `.write` 決定。 |
| `dl_runtime_ioctl_set_message()` 為什麼要建立 `struct dl_ioctl_message`？ | 因為 Lab03 UAPI 定義 `DL_IOC_SET_MESSAGE` 的 payload 是固定大小 struct，不是原始 C 字串。 |
| `DL_IOC_GET_STATUS` 的 status 是誰填的？ | kernel driver 在 `dl_unlocked_ioctl()` 裡填好，再 `copy_to_user()` 回 userspace。 |
| `poll()` 的回傳值和 `revents` 差在哪？ | 回傳值是有事件的 fd 數量；`revents` 是該 fd 實際發生的 event mask。 |
| `dl_runtime_mmap_shared()` 失敗時回什麼？ | `MAP_FAILED`，也就是 `(void *) -1`，不是 `NULL`。 |
| `mmap-read` 看到的 struct layout 由哪裡定義？ | [`../include/driver_lab_uapi.h`](../include/driver_lab_uapi.h) 的 `struct dl_shared_page`。 |

## 查證來源

- Linux man-pages `open(2)`：說明成功回傳非負 fd、失敗回 `-1` 並設定 `errno`。<https://man7.org/linux/man-pages/man2/open.2.html>
- Linux man-pages `poll(2)`：說明 `struct pollfd`、`events`/`revents`、timeout、return value、`POLLIN`/`POLLPRI`。<https://man7.org/linux/man-pages/man2/poll.2.html>
- Linux man-pages `mmap(2)`：說明 `mmap()` / `munmap()` return value、`MAP_FAILED`、`length == 0` 的 `EINVAL`、以及 `munmap()` 後 mapping invalid。<https://man7.org/linux/man-pages/man2/mmap.2.html>
- Linux kernel documentation `Ioctl Numbers`：說明 `_IO/_IOW/_IOR/_IOWR` 與 read/write 方向是從 userspace 觀點定義。<https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html>
