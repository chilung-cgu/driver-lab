# `driver_lab_uapi.h` 詳解

## 這份檔案的角色

這是 Lab02/Lab03 userspace 和 kernel 共同遵守的 ABI header。UAPI 是 userspace API 的意思；只要放進這份檔案，就代表 kernel driver、runtime、CLI 必須對欄位大小、command number、layout 有相同理解。

被這些檔案使用：

- kernel driver：[`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)
- runtime 實作：[`../src/driver_lab_runtime.c.md`](../src/driver_lab_runtime.c.md)
- runtime header：[`driver_lab_runtime.h.md`](driver_lab_runtime.h.md)
- CLI：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)

## 先讀哪裡

第一次照 ABI 類型讀：

1. 常數：`DL_MESSAGE_BYTES`、`DL_MMAP_BYTES`、`DL_SHARED_MAGIC`、`DL_IOCTL_TYPE`
2. ioctl payload：`struct dl_ioctl_message`
3. ioctl status：`struct dl_ioctl_status`
4. mmap layout：`struct dl_shared_page`
5. command number：`DL_IOC_*`

## 主線資料流

```text
driver_lab_uapi.h
  -> kernel driver 用它解讀 ioctl arg 和 shared page layout
  -> runtime 用它建立 ioctl payload
  -> CLI 用它解讀 status/mmap-read 結果
```

這份檔案一旦改動，影響的是 kernel/userspace 邊界，不只是單一 C 檔。

## 分區詳解

### kernel/userspace include 差異

```c
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
```

同一份 header 會被 kernel module 和 userspace 程式 include。兩邊可用的 ioctl macro header 不同，所以用 `__KERNEL__` 分流。

### ABI size constants

```c
#define DL_MESSAGE_BYTES 256U
#define DL_MMAP_BYTES 4096U
#define DL_SHARED_MAGIC 0x444C4150U
#define DL_IOCTL_TYPE 'L'
```

這些不是普通內部常數，而是 ABI 的一部分。

- `DL_MESSAGE_BYTES`：message buffer 最大固定大小。
- `DL_MMAP_BYTES`：shared mapping 的大小，目前是一頁。
- `DL_SHARED_MAGIC`：userspace 判斷 mmap layout 是否像預期。
- `DL_IOCTL_TYPE`：ioctl command number 的 type field。

### `struct dl_ioctl_message`

這是 `DL_IOC_SET_MESSAGE` 的 payload。runtime 會把 C 字串放進 `text`，driver 用 `copy_from_user()` 收進 kernel。

固定大小陣列的好處是教學上簡單，缺點是長度上限成為 ABI，需要明確處理過長輸入。

### `struct dl_ioctl_status`

這是 `DL_IOC_GET_STATUS` 的回傳 struct。

| 欄位 | 意義 |
|---|---|
| `buffer_len` | 目前 driver buffer 長度 |
| `event_count` | 累積事件數 |
| `event_pending` | 是否有 pending event |
| `mmap_size` | userspace 應該 mmap 的大小 |

CLI 的 `status` subcommand 會印出這些欄位。

### `struct dl_shared_page`

這是 `mmap-read` 看到的 shared page layout。userspace mmap 後會把回傳 pointer 轉成 `struct dl_shared_page *`。

欄位順序和大小都重要，因為 userspace 直接照這個 layout 讀 memory。這也是為什麼不能隨便把 kernel private pointer 或 lock 放進 UAPI struct。

### `DL_IOC_*` command

| command | macro | payload 方向 |
|---|---|---|
| `DL_IOC_SET_MESSAGE` | `_IOW` | userspace -> kernel |
| `DL_IOC_GET_STATUS` | `_IOR` | kernel -> userspace |
| `DL_IOC_TRIGGER_EVENT` | `_IO` | 無 payload |
| `DL_IOC_CLEAR_BUFFER` | `_IO` | 無 payload |

`_IOW/_IOR/_IO` 會把 type、number、payload type 等資訊編進 command number。第一輪不需要背位元細節，但要知道這些 command number 是 ABI。

## ABI 修改風險

| 改動 | 風險 |
|---|---|
| 改 `DL_MESSAGE_BYTES` | kernel/runtime/CLI 對 message 上限理解可能不同 |
| 改 struct 欄位順序 | mmap/status 解析會錯位 |
| 改 `DL_IOCTL_TYPE` 或 command number | 舊 runtime 可能打不到新 driver command |
| 加 kernel-only pointer 到 shared struct | userspace 看到無意義甚至危險的 kernel implementation detail |

## 常見卡點

- UAPI header 不應放 kernel private state，例如 mutex、waitqueue、`struct cdev`。
- `_IOW` 的 W 是從 userspace 視角「write to kernel」。
- `struct dl_shared_page` 是 mmap ABI，不是 driver 內部唯一真相；driver 內部 state 仍在 `driver_lab_ioctl_poll_mmap.c`。
- magic/version 不能保證所有相容性，但能讓 userspace 有基本 sanity check。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 為什麼這份 header 同時處理 kernel/userspace include？ | 因為它被 kernel module 和 userspace runtime/CLI 共用。 |
| `DL_IOC_SET_MESSAGE` 的 payload 是什麼？ | `struct dl_ioctl_message`。 |
| `mmap-read` 依照哪個 struct 解讀 shared page？ | `struct dl_shared_page`。 |
| 為什麼改這份檔案要小心？ | 它定義 kernel/userspace ABI，改動可能破壞相容性。 |
