# `driver_lab_uapi.h` 詳解

## 結論

`runtime/include/driver_lab_uapi.h` 是 Lab03 的 kernel/userspace 合約。kernel driver、runtime、CLI 都 include 它，所以它定義的東西不是普通內部實作細節，而是 ABI：

```text
ioctl command number
ioctl payload struct
status struct
mmap shared page layout
buffer size / mmap size / magic value
```

如果你讀 Lab03 時常常卡在「driver 和 runtime 到底怎麼知道彼此要傳什麼」，答案大多在這份檔案。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`driver_lab_uapi.h`](driver_lab_uapi.h) 本身。
- 使用它的 kernel driver：[`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。
- 使用它的 runtime：[`../src/driver_lab_runtime.c.md`](../src/driver_lab_runtime.c.md)。
- 使用它的 CLI：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)。
- ioctl command macro 的方向語意以 Linux kernel documentation `Ioctl Numbers` 為準。

這份文件不討論跨 architecture ABI padding/alignment 的完整產品級策略；Lab03 目前用的是教學用固定大小 `unsigned int` 與 fixed array layout。

## 先理解這份檔案在 repo 的位置

這份 header 被兩邊共用：

```text
kernel side:
  labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c

userspace side:
  runtime/include/driver_lab_runtime.h
  runtime/src/driver_lab_runtime.c
  tests/driver_lab_char_cli.c
```

白話講：

```text
driver_lab_uapi.h 是雙方共同看的契約
driver 不能自己想一套 struct
runtime/CLI 也不能自己猜 command number
```

## 這份檔案要解決什麼問題？

`ioctl()` 和 `mmap()` 不是只靠 function name 就能知道資料格式。雙方需要一份共同定義：

- userspace 送 `DL_IOC_SET_MESSAGE` 時，payload 長什麼樣？
- userspace 查 `DL_IOC_GET_STATUS` 時，driver 回傳哪些欄位？
- userspace `mmap-read` 時，映射回來的一頁要怎麼解讀？
- message 最多幾 bytes？
- shared page 多大？
- userspace 怎麼檢查這頁像不像預期 layout？

這些都在 `driver_lab_uapi.h`。

## 一、include guard

原始碼：

```c
#ifndef DRIVER_LAB_UAPI_H
#define DRIVER_LAB_UAPI_H
...
#endif
```

include guard 避免同一個 translation unit 重複 include 時造成重複定義。

這對 UAPI header 特別重要，因為它會被多個 userspace source、kernel source、runtime header 間接 include。

## 二、kernel/userspace include 分流

原始碼：

```c
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
```

同一份 header 會被 kernel 和 userspace include，但兩邊可用的 ioctl macro header 不同：

| 編譯端 | include |
|---|---|
| kernel module | `<linux/ioctl.h>` |
| userspace C 程式 | `<sys/ioctl.h>` |

`__KERNEL__` 是 kernel build 時會定義的條件。這讓同一份 UAPI header 能在兩邊都編過。

常見誤解：

```text
UAPI header 不是只能給 userspace
它通常也會被 kernel driver include
因為 driver 也要知道同一份 command/struct 定義
```

## 三、ABI size constants

原始碼：

```c
#define DL_MESSAGE_BYTES 256U
#define DL_MMAP_BYTES 4096U
#define DL_SHARED_MAGIC 0x444C4150U
#define DL_IOCTL_TYPE 'L'
```

逐一看：

| 常數 | 意義 | 誰使用 |
|---|---|---|
| `DL_MESSAGE_BYTES` | message buffer 固定大小 | driver buffer、ioctl message、shared page buffer |
| `DL_MMAP_BYTES` | userspace mmap 建議大小 | runtime/CLI mmap-read、driver status |
| `DL_SHARED_MAGIC` | shared page sanity check | driver 寫入、CLI 印出 |
| `DL_IOCTL_TYPE` | ioctl command type field | `DL_IOC_*` macros |

這些是 ABI 的一部分。改掉它們不只是改一個常數，而是改 kernel/userspace 合約。

白話講：

```text
如果 driver 以為 message 是 256 bytes
但 userspace 以為是 512 bytes
那 ioctl payload 的理解就會不一致
```

## 四、`struct dl_ioctl_message`：set-message 的 payload

原始碼：

```c
struct dl_ioctl_message {
	char text[DL_MESSAGE_BYTES];
};
```

這個 struct 用在：

```c
#define DL_IOC_SET_MESSAGE _IOW(DL_IOCTL_TYPE, 0x01, struct dl_ioctl_message)
```

userspace runtime 會做：

```c
struct dl_ioctl_message msg;
memset(&msg, 0, sizeof(msg));
memcpy(msg.text, message, len);
ioctl(fd, DL_IOC_SET_MESSAGE, &msg);
```

kernel driver 會做：

```c
copy_from_user(&msg, (void __user *)arg, sizeof(msg));
```

白話講：

```text
CLI 傳進 runtime 的是 C 字串
runtime 包成 struct dl_ioctl_message
driver 再 copy_from_user 進 kernel
```

## 五、`struct dl_ioctl_status`：get-status 的回傳

原始碼：

```c
struct dl_ioctl_status {
	unsigned int buffer_len;
	unsigned int event_count;
	unsigned int event_pending;
	unsigned int mmap_size;
};
```

這個 struct 用在：

```c
#define DL_IOC_GET_STATUS _IOR(DL_IOCTL_TYPE, 0x02, struct dl_ioctl_status)
```

driver 填入：

| 欄位 | driver state |
|---|---|
| `buffer_len` | `dl_buffer_len` |
| `event_count` | `dl_event_count` |
| `event_pending` | `dl_event_pending ? 1U : 0U` |
| `mmap_size` | `DL_MMAP_BYTES` |

CLI 印出：

```text
buffer_len=11 event_count=1 event_pending=1 mmap_size=4096
```

白話講：

```text
status ioctl 是 userspace 觀察 driver 狀態的低成本窗口
不用 read data path，也不用 mmap shared page
```

## 六、`struct dl_shared_page`：mmap-read 的 layout

原始碼：

```c
struct dl_shared_page {
	unsigned int magic;
	unsigned int version;
	unsigned int event_count;
	unsigned int event_pending;
	unsigned int buffer_len;
	char buffer[DL_MESSAGE_BYTES];
};
```

這個 struct 是 `mmap()` 後 userspace 直接讀的 layout。

driver 端：

```c
page = (struct dl_shared_page *)dl_shared_page_addr;
page->magic = DL_SHARED_MAGIC;
page->version = 1;
page->event_count = dl_event_count;
page->event_pending = dl_event_pending ? 1U : 0U;
page->buffer_len = dl_buffer_len;
memcpy(page->buffer, dl_buffer, dl_buffer_len);
```

CLI 端：

```c
struct dl_shared_page *shared;
shared = dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
printf("magic=0x%x version=%u ...\n", shared->magic, shared->version, ...);
```

白話講：

```text
mmap 回來的是一段 memory
userspace 需要照 struct dl_shared_page 的格式去讀
否則只是一堆 bytes
```

為什麼要有 `magic` 和 `version`？

- `magic`：基本 sanity check，確認這頁看起來像 driver_lab shared page。
- `version`：未來 layout 演進時可以區分版本。

## 七、`DL_IOC_*` command number

原始碼：

```c
#define DL_IOC_SET_MESSAGE _IOW(DL_IOCTL_TYPE, 0x01, struct dl_ioctl_message)
#define DL_IOC_GET_STATUS _IOR(DL_IOCTL_TYPE, 0x02, struct dl_ioctl_status)
#define DL_IOC_TRIGGER_EVENT _IO(DL_IOCTL_TYPE, 0x03)
#define DL_IOC_CLEAR_BUFFER _IO(DL_IOCTL_TYPE, 0x04)
```

方向要從 userspace 觀點看：

| macro | userspace 視角 | Lab03 command |
|---|---|---|
| `_IOW` | userspace write to kernel | `DL_IOC_SET_MESSAGE` |
| `_IOR` | userspace read from kernel | `DL_IOC_GET_STATUS` |
| `_IO` | 沒有額外 payload | `DL_IOC_TRIGGER_EVENT`、`DL_IOC_CLEAR_BUFFER` |

新手常會被 `_IOW` / `_IOR` 的方向搞混。以 `DL_IOC_SET_MESSAGE` 為例：

```text
userspace 覺得自己在 write message 給 kernel
所以 macro 是 _IOW

kernel implementation 裡則是 copy_from_user()
因為 kernel 要從 userspace pointer 複製進 kernel
```

## ABI 修改風險

| 改動 | 可能造成什麼問題 |
|---|---|
| 改 `DL_MESSAGE_BYTES` | driver/runtime/CLI 對 buffer 大小理解不同 |
| 改 `struct dl_ioctl_status` 欄位順序 | CLI 印出的 status 可能錯位 |
| 改 `struct dl_shared_page` 欄位順序 | mmap-read 解讀錯誤 |
| 改 command number | 舊 runtime 送出的 ioctl command driver 不認得 |
| 把 kernel pointer 放進 UAPI struct | userspace 看到 kernel implementation detail，且沒有可移植意義 |

## 這份檔案和其他檔案的對照

| UAPI 定義 | runtime 使用 | driver 使用 | CLI 使用 |
|---|---|---|---|
| `DL_MESSAGE_BYTES` | 檢查 message 長度 | `dl_buffer` 大小 | read buffer 間接對應 |
| `struct dl_ioctl_message` | 建立 set-message payload | `copy_from_user()` 收 payload | 不直接操作 |
| `struct dl_ioctl_status` | get-status output | 填狀態後 `copy_to_user()` | 印出 status |
| `struct dl_shared_page` | mmap 回傳後的 type | 同步 shared snapshot | `mmap-read` 印欄位 |
| `DL_IOC_*` | `ioctl()` command | `switch (cmd)` 分派 | 透過 runtime 間接使用 |

## 常見卡點

- UAPI 不是 driver private header；它是 kernel/userspace 合約。
- `_IOW` / `_IOR` 是從 userspace 觀點命名。
- `struct dl_shared_page` 的欄位順序是 ABI，不是隨便整理美觀用。
- `DL_MMAP_BYTES` 目前是 4096，但 driver 的 `dl_mmap()` 仍會檢查 mapping size 不超過 `PAGE_SIZE`。
- `magic` 只能做基本 sanity check，不能保證完整相容性。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這份 header 為什麼 kernel/userspace 都 include？ | 因為它定義雙方共同遵守的 ABI。 |
| `DL_IOC_SET_MESSAGE` 的 payload type 是什麼？ | `struct dl_ioctl_message`。 |
| `DL_IOC_GET_STATUS` 用 `_IOR` 還是 `_IOW`？ | `_IOR`，從 userspace 視角是 read status from kernel。 |
| `mmap-read` 用哪個 struct 解讀 memory？ | `struct dl_shared_page`。 |
| 為什麼不能把 `struct mutex` 放進 UAPI？ | 那是 kernel private state，不是 userspace ABI。 |
| 改 `DL_MESSAGE_BYTES` 會影響哪些檔案？ | driver、runtime、CLI 對 message/shared page layout 的理解都可能受影響。 |

## 查證來源

- Linux kernel documentation `Ioctl Numbers`：`_IO/_IOW/_IOR/_IOWR` 和 direction 語意。<https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html>
