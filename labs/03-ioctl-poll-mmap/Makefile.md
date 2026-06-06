# `Makefile` 詳解

## 這份檔案的角色

這是 Lab03 kernel module 的 build glue。它告訴 Linux kbuild：把 [`driver_lab_ioctl_poll_mmap.c`](driver_lab_ioctl_poll_mmap.c) 建成 `driver_lab_ioctl_poll_mmap.ko`。

這份 Makefile 不是一般 userspace C 專案的 Makefile。你不應該用 `gcc driver_lab_ioctl_poll_mmap.c` 直接編 kernel module；外掛 module 要交給目前 Linux kernel 的 kbuild。

## 先讀哪裡

第一次只要抓四個點：

1. `obj-m += driver_lab_ioctl_poll_mmap.o`：這個 lab 要建哪個 module。
2. `ccflags-y += ...`：為什麼 kbuild 找得到 runtime 的 UAPI header。
3. `KDIR ?= /lib/modules/$(shell uname -r)/build`：目前 kernel build tree 在哪。
4. `all` / `clean`：實際呼叫 kbuild 的入口。

## 分區詳解

### `obj-m`

```make
obj-m += driver_lab_ioctl_poll_mmap.o
```

`obj-m` 是 kbuild 的變數。它代表這個目錄要建外掛 module。kbuild 會從 `driver_lab_ioctl_poll_mmap.o` 對應到 `driver_lab_ioctl_poll_mmap.c`，最後產生 `driver_lab_ioctl_poll_mmap.ko`。

### `ccflags-y`

```make
ccflags-y += -I$(src)/../../runtime/include
```

Lab03 driver include 了：

```c
#include "../../runtime/include/driver_lab_uapi.h"
```

這個 include 在目前 source 裡已經是相對路徑；`ccflags-y` 也把 UAPI header 目錄交給 kbuild。重點是：kernel driver 和 userspace runtime 必須看到同一份 [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md)，否則 ioctl struct/command/shared page layout 可能不一致。

### `KDIR`

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

這通常是一個 symlink，指向目前正在跑的 kernel 對應的 build tree。外掛 module 必須用對應 kernel 的 headers/config 來建。

如果你在 macOS 執行，這個路徑不存在；所以 Lab03 的 build/load/smoke test 要在 Linux host 或 Linux guest 上跑。

### `PWD`

```make
PWD := $(shell pwd)
```

`M=$(PWD)` 會把目前 lab 目錄傳給 kbuild，告訴它 external module source 在這裡。

### targets

```make
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

`-C $(KDIR)` 代表先切到 kernel build tree，`M=$(PWD)` 代表回頭建這個 external module 目錄。這是 out-of-tree kernel module 的標準形狀。

`modules_install` 保留標準 kbuild 目標，但一般學習流程不需要跑。

## 關鍵變數

| 變數 | 意義 |
|---|---|
| `obj-m` | 要建成 `.ko` 的 module object |
| `ccflags-y` | 傳給 kbuild 的額外 C flags |
| `KDIR` | kernel build tree |
| `PWD` | Lab03 source 目錄 |
| `M=$(PWD)` | 告訴 kbuild 這是 external module |

## 常見卡點

- `make` 失敗且找不到 `/lib/modules/.../build`：通常代表 Linux kernel headers/build tree 沒裝好，或你不在 Linux。
- 不要用 plain `gcc` 編 kernel module；kernel module 需要 kbuild 提供的 include path、config、module metadata。
- `make clean` 只清 kbuild artifact，不會卸載已載入的 module；卸載要用 `sudo rmmod driver_lab_ioctl_poll_mmap`。
- runtime CLI 不是這份 Makefile 建的；CLI 由 [`../../runtime/Makefile`](../../runtime/Makefile) 建。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab03 最後產生的 kernel module 是什麼？ | `driver_lab_ioctl_poll_mmap.ko`。 |
| `obj-m` 的角色是什麼？ | 告訴 kbuild 哪個 object 要建成外掛 module。 |
| 為什麼要 `KDIR`？ | external module 必須透過目前 kernel 對應的 build tree 建置。 |
| CLI 是這份 Makefile 建的嗎？ | 不是，CLI 由 `runtime/Makefile` 建。 |
