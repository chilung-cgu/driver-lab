# `Makefile` 詳解

## 結論

`labs/03-ioctl-poll-mmap/Makefile` 是 Lab03 的 kbuild external module 入口。它不是用一般 `gcc driver.c` 編 kernel module，而是把目前目錄交給 Linux kernel build system：

```sh
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

最後產生：

```text
driver_lab_ioctl_poll_mmap.ko
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab03 driver：[`driver_lab_ioctl_poll_mmap.c.md`](driver_lab_ioctl_poll_mmap.c.md)。
- Linux kernel documentation `Building External Modules`。

這份文件只解釋目前 Lab03 的 kbuild 用法，不展開整個 kernel kbuild 語法。

## 先理解這份檔案在 repo 的位置

Lab03 目錄裡：

```text
driver_lab_ioctl_poll_mmap.c
Makefile
test.sh
```

執行：

```sh
cd labs/03-ioctl-poll-mmap
make
```

會呼叫目前 Linux kernel 的 build tree，產生 `.ko`。

## 這份檔案要解決什麼問題？

kernel module 不能用普通 userspace compile 方式：

```sh
gcc driver_lab_ioctl_poll_mmap.c
```

原因是 kernel module 需要：

- kernel headers。
- kernel config。
- module metadata。
- kbuild 規則。
- 和目前執行 kernel 相容的 build environment。

所以 Makefile 的任務是把 Lab03 source 交給 kbuild。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_ioctl_poll_mmap.o
```

`obj-m` 是 kbuild 變數，代表要建 external module。

kbuild 會從：

```text
driver_lab_ioctl_poll_mmap.o
```

找到：

```text
driver_lab_ioctl_poll_mmap.c
```

最後輸出：

```text
driver_lab_ioctl_poll_mmap.ko
```

## 二、`ccflags-y`

原始碼：

```make
ccflags-y += -I$(src)/../../runtime/include
```

Lab03 driver include 共用 UAPI：

```c
#include "../../runtime/include/driver_lab_uapi.h"
```

`ccflags-y` 把 runtime include path 傳給 kbuild。這讓 kernel driver build 時能找到同一份 UAPI header。

白話講：

```text
driver 和 runtime/CLI 必須看同一份 ABI
所以 kbuild 要知道 UAPI header 在哪
```

## 三、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 kernel 的 build tree。`?=` 表示 caller 可以覆蓋：

```sh
make KDIR=/path/to/kernel/build
```

預設值通常是 Linux distro 安裝 kernel headers 後提供的 symlink。

如果這個路徑不存在，通常代表：

- 沒有安裝 kernel headers。
- 不在 Linux 上。
- kernel headers 和 running kernel 不匹配。

## 四、`PWD` 與 `M=$(PWD)`

原始碼：

```make
PWD := $(shell pwd)
```

後面 target 用：

```make
$(MAKE) -C $(KDIR) M=$(PWD) modules
```

意思是：

```text
-C $(KDIR)
  進入 kernel build tree

M=$(PWD)
  告訴 kbuild external module source 在目前 Lab03 目錄
```

這是 Linux kernel documentation 建議的 external module build 形狀。

## 五、`all`

原始碼：

```make
all:
	# 建出 driver_lab_ioctl_poll_mmap.ko。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

執行 `make` 時預設跑 `all`，也就是建 module。

展開概念：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/03-ioctl-poll-mmap \
  modules
```

## 六、`clean`

原始碼：

```make
clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

`make clean` 清 build artifact，例如 `.o`、`.ko`、`.mod.*`、`.tmp_versions` 等。

注意：

```text
make clean 不會 rmmod
如果 module 已經載入，仍然要 sudo rmmod driver_lab_ioctl_poll_mmap
```

## 七、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild 目標，用來安裝 module 到系統 module tree。Lab03 一般學習流程不需要跑，因為你只需要在 lab 目錄手動 `insmod ./driver_lab_ioctl_poll_mmap.ko`。

## 常見卡點

- 不要用 plain `gcc` 編 kernel module。
- macOS 上沒有 `/lib/modules/$(uname -r)/build`，所以不能跑這份 Makefile build `.ko`。
- `make clean` 不等於卸載 module。
- `KDIR` 可以覆蓋，但要指到相容 kernel build tree。
- `ccflags-y` 是給 kbuild 的 C flags，不是一般 Makefile 的 `CFLAGS`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `obj-m` 代表什麼？ | 要建成 external kernel module 的 object。 |
| Lab03 輸出的 module 檔名是什麼？ | `driver_lab_ioctl_poll_mmap.ko`。 |
| `KDIR` 預設指到哪裡？ | `/lib/modules/$(uname -r)/build`。 |
| `M=$(PWD)` 的作用？ | 告訴 kbuild external module source 在目前目錄。 |
| `make clean` 會卸載 module 嗎？ | 不會，只清 build artifact。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD` 建置。<https://docs.kernel.org/kbuild/modules.html>
