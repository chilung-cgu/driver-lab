# `Makefile` 詳解

## 結論

`labs/00-hello-module/Makefile` 是 driver-lab 的第一個 kbuild external module Makefile。它把 [`driver_lab_hello.c`](driver_lab_hello.c) 交給目前 Linux kernel 的 build tree，產生：

```text
driver_lab_hello.ko
```

這份 Makefile 教你一件最重要的事：

```text
kernel module 不要用 plain gcc 編
要透過 kbuild
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab00 source：[`driver_lab_hello.c.md`](driver_lab_hello.c.md)。
- Linux kernel documentation `Building External Modules`。

不展開所有 kbuild 變數，只解釋 Lab00 需要的最小 external module build path。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_hello.o
```

`obj-m` 告訴 kbuild：這個目錄要建一個外掛 module。

kbuild 會把：

```text
driver_lab_hello.o
```

對應到：

```text
driver_lab_hello.c
```

最後輸出：

```text
driver_lab_hello.ko
```

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

這是目前 running kernel 對應的 build tree。外掛 module 要使用和 kernel 相容的 headers/config/build rules。

如果這個路徑不存在，常見原因是：

- 不在 Linux。
- 沒安裝目前 kernel 的 headers。
- running kernel 和 headers 不匹配。

`?=` 表示可以覆蓋：

```sh
make KDIR=/path/to/kernel/build
```

## 三、`PWD` 和 `M=$(PWD)`

原始碼：

```make
PWD := $(shell pwd)
```

後面用：

```make
$(MAKE) -C $(KDIR) M=$(PWD) modules
```

意思是：

```text
-C $(KDIR)
  進入 kernel build tree

M=$(PWD)
  告訴 kbuild external module source 在 Lab00 目錄
```

官方 external module 文件也使用這個形狀。

## 四、`all`

原始碼：

```make
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

執行：

```sh
make
```

就會跑 `all`，呼叫 kbuild 建 module。

概念展開：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/00-hello-module \
  modules
```

## 五、`clean`

原始碼：

```make
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

這會清掉 kbuild 產物，例如 `.o`、`.ko`、`.mod.*`、`Module.symvers`、`modules.order`。

注意：

```text
make clean 不會 rmmod
已載入的 module 要先 sudo rmmod driver_lab_hello
```

## 六、`modules_install`

原始碼：

```make
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild 目標，用於安裝 module 到系統 module tree。Lab00 一般不需要執行；學習流程用 `insmod ./driver_lab_hello.ko` 即可。

## 常見卡點

- macOS 不能用這份 Makefile build Linux kernel module。
- 不要用 `gcc driver_lab_hello.c`。
- `make clean` 只清 build artifact，不卸載 kernel 裡已載入的 module。
- `KDIR` 必須對應你要 build 的 kernel。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab00 最後建出的 module 檔名是什麼？ | `driver_lab_hello.ko`。 |
| `obj-m` 的用途？ | 告訴 kbuild 要建 external module。 |
| `KDIR` 預設是什麼？ | `/lib/modules/$(uname -r)/build`。 |
| `M=$(PWD)` 代表什麼？ | external module source 在目前目錄。 |
| `make clean` 會卸載 module 嗎？ | 不會。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`。<https://docs.kernel.org/kbuild/modules.html>
