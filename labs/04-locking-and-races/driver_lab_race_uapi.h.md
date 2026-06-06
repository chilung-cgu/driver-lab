# `driver_lab_race_uapi.h` 詳解

## 結論

`labs/04-locking-and-races/driver_lab_race_uapi.h` 是 Lab04 的 kernel/userspace 共用 ABI header。kernel driver [`driver_lab_race.c`](driver_lab_race.c) 和 userspace CLI [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c) 都 include 這份檔案。

它定義兩件事：

1. `struct dl_race_status`：`GET_STATUS` 回傳的結構化狀態。
2. `DL_RACE_IOC_*`：userspace 透過 `ioctl()` 下給 driver 的 command number。

這份檔案是 ABI，不是 driver private header。欄位順序、型別、ioctl number 都不能隨便改，否則舊 CLI 和新 driver 可能對不上。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`driver_lab_race_uapi.h`](driver_lab_race_uapi.h) 本身。
- kernel 使用端：[`driver_lab_race.c.md`](driver_lab_race.c.md)。
- userspace 使用端：[`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md)。
- Linux kernel documentation：ioctl command number 與 ioctl interface。

這裡不展開 ioctl number 的完整 bit layout；第一輪只解釋 `_IO` / `_IOW` / `_IOR` 在本 lab 的方向與 ABI 角色。

## 一、include guard 與 kernel/userspace include 分流

原始碼：

```c
#ifndef DRIVER_LAB_RACE_UAPI_H
#define DRIVER_LAB_RACE_UAPI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
```

這份 header 同時被 kernel 和 userspace include，所以要分流：

| 編譯位置 | include |
|---|---|
| kernel module | `<linux/ioctl.h>` |
| userspace CLI | `<sys/ioctl.h>` |

`__KERNEL__` 是 kernel build 環境會定義的 macro。這讓同一份 UAPI header 能同時提供 `_IO*` macro，又不把 kernel-only header 直接丟給 userspace。

## 二、`struct dl_race_status`

原始碼：

```c
struct dl_race_status {
	unsigned int counter;
	unsigned int safe_mode;
	unsigned int worker_running;
};
```

這個 struct 是 `DL_RACE_IOC_GET_STATUS` 的資料格式。

欄位：

| 欄位 | 來源 state | 意義 |
|---|---|---|
| `counter` | `dl_counter` | 目前 shared counter 值。 |
| `safe_mode` | `dl_safe_mode` | `0` 表示 unsafe，`1` 表示 mutex safe path。 |
| `worker_running` | `dl_worker_running` | background kthread 是否仍被 driver 視為 running。 |

kernel 端：

```c
status.counter = dl_counter;
status.safe_mode = dl_safe_mode ? 1U : 0U;
status.worker_running = dl_worker_running ? 1U : 0U;
copy_to_user((void __user *)arg, &status, sizeof(status));
```

userspace 端：

```c
struct dl_race_status status;
ioctl(fd, DL_RACE_IOC_GET_STATUS, &status);
printf("counter=%u safe_mode=%u worker_running=%u\n", ...);
```

ABI 注意事項：

- 不要放 kernel pointer。
- 不要用 userspace/kernel 可能大小不同的型別，例如裸 `long`。
- 不要隨意改欄位順序；舊 binary 會照舊 layout 解讀。

## 三、ioctl type

原始碼：

```c
#define DL_RACE_IOCTL_TYPE 'R'
```

`'R'` 是這組 ioctl command 的識別 type。它會成為 `_IO*` command number 的一部分。

這不是 security boundary；它只是幫 ioctl number 更可辨識，也讓工具如 `strace` 較容易 decode。

## 四、`DL_RACE_IOC_SET_SAFE_MODE`

原始碼：

```c
#define DL_RACE_IOC_SET_SAFE_MODE _IOW(DL_RACE_IOCTL_TYPE, 0x01, unsigned int)
```

`_IOW` 的 `W` 是從 userspace 角度看：

```text
userspace writes data to kernel
```

在 kernel driver 裡對應：

```c
copy_from_user(&safe_mode, (void __user *)arg, sizeof(safe_mode))
```

也就是：

```text
CLI safe-mode 0/1
  -> unsigned int value
  -> DL_RACE_IOC_SET_SAFE_MODE
  -> driver 更新 dl_safe_mode
```

## 五、`DL_RACE_IOC_GET_STATUS`

原始碼：

```c
#define DL_RACE_IOC_GET_STATUS _IOR(DL_RACE_IOCTL_TYPE, 0x02, struct dl_race_status)
```

`_IOR` 的 `R` 也是從 userspace 角度看：

```text
userspace reads data from kernel
```

在 kernel driver 裡對應：

```c
copy_to_user((void __user *)arg, &status, sizeof(status))
```

也就是：

```text
driver 填 struct dl_race_status
  -> copy_to_user()
  -> CLI 印 counter/safe_mode/worker_running
```

## 六、`DL_RACE_IOC_INC_COUNTER`

原始碼：

```c
#define DL_RACE_IOC_INC_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x03)
```

`_IO` 代表沒有額外 payload。userspace 只要下 command：

```c
ioctl(fd, DL_RACE_IOC_INC_COUNTER)
```

driver 就跑：

```c
dl_race_increment();
```

這是 `inc` 和 `race` subcommand 的核心。

## 七、`DL_RACE_IOC_RESET_COUNTER`

原始碼：

```c
#define DL_RACE_IOC_RESET_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x04)
```

這也沒有 payload。driver 收到後：

```c
mutex_lock(&dl_race_lock);
dl_counter = 0;
mutex_unlock(&dl_race_lock);
```

每輪 race 實驗前都 reset，避免上一輪數字干擾觀察。

## command 對照表

| CLI subcommand | ioctl command | payload 方向 | payload type |
|---|---|---|---|
| `safe-mode 0|1` | `DL_RACE_IOC_SET_SAFE_MODE` | user -> kernel | `unsigned int` |
| `status` | `DL_RACE_IOC_GET_STATUS` | kernel -> user | `struct dl_race_status` |
| `inc <count>` | `DL_RACE_IOC_INC_COUNTER` | none | none |
| `race <threads> <loops>` | `DL_RACE_IOC_INC_COUNTER` | none | none |
| `reset` | `DL_RACE_IOC_RESET_COUNTER` | none | none |

## 常見卡點

- `_IOW` / `_IOR` 的方向是從 userspace 角度命名，不是 kernel 角度。
- `struct dl_race_status` 是 ABI struct，不要放 kernel private pointer。
- `_IOW(..., unsigned int)` 第三個參數是型別，不要寫 `sizeof(unsigned int)`。
- 改 ioctl command number 會讓舊 CLI 和新 driver 對不上。
- `inc` 和 `reset` 沒 payload，所以用 `_IO`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 哪兩個檔案共同 include 這份 UAPI？ | kernel driver `driver_lab_race.c` 和 userspace CLI `tests/driver_lab_race_cli.c`。 |
| `safe-mode` 對應哪個 ioctl？ | `DL_RACE_IOC_SET_SAFE_MODE`。 |
| `status` 回傳什麼 struct？ | `struct dl_race_status`。 |
| `_IOW` 在這裡代表什麼方向？ | userspace 把 `unsigned int` safe mode 值寫給 kernel。 |
| `_IOR` 在這裡代表什麼方向？ | userspace 從 kernel 讀回 `struct dl_race_status`。 |
| 為什麼不能隨便改 struct 欄位？ | 這是 kernel/userspace ABI，舊 binary 會照舊 layout 解讀。 |

## 查證來源

- Linux kernel documentation `ioctl based interfaces`：`_IO`、`_IOR`、`_IOW`、`_IOWR` 的用途與 command number 定義建議。<https://docs.kernel.org/driver-api/ioctl.html>
- Linux kernel documentation `Ioctl Numbers`：`_IOW` / `_IOR` 的方向是從 userspace 角度命名。<https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html>
