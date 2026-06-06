# `Makefile` 詳解

## 結論

`labs/01-debugfs-logging/Makefile` 是 Lab01 的 kbuild external module 入口。它把 [`driver_lab_debugfs_logging.c`](driver_lab_debugfs_logging.c) 建成：

```text
driver_lab_debugfs_logging.ko
```

和 Lab00 一樣，這不是一般 userspace Makefile；kernel module 要透過 Linux kbuild 建置。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab01 driver：[`driver_lab_debugfs_logging.c.md`](driver_lab_debugfs_logging.c.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab01 使用到的 kbuild 形狀，不展開完整 kernel Makefile 語法。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_debugfs_logging.o
```

這告訴 kbuild：把 `driver_lab_debugfs_logging.c` 編成 external module。

對照：

```text
driver_lab_debugfs_logging.c
  -> driver_lab_debugfs_logging.o
  -> driver_lab_debugfs_logging.ko
```

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

這指向目前 running kernel 的 build tree。

如果 `make` 失敗並提到找不到 build tree，第一個檢查：

```sh
ls /lib/modules/$(uname -r)/build
```

常見原因是 kernel headers 沒裝，或不是在 Linux 上 build。

## 三、`PWD` 與 `M=$(PWD)`

原始碼：

```make
PWD := $(shell pwd)
```

target 裡：

```make
$(MAKE) -C $(KDIR) M=$(PWD) modules
```

意思是：

```text
-C $(KDIR)
  進入 kernel build tree

M=$(PWD)
  告訴 kbuild external module source 在 Lab01 目錄
```

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

就會建出 `.ko`。

## 五、`clean`

原始碼：

```make
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

清掉 kbuild artifact，例如 `.o`、`.ko`、`.mod.*`、`Module.symvers`、`modules.order`。

注意：

```text
make clean 不會卸載已經 insmod 的 module
```

## 六、`modules_install`

原始碼：

```make
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

保留標準 kbuild 目標。Lab01 一般學習流程不需要把 module 安裝進系統 module tree。

## 常見卡點

- macOS 不能 build/load Linux kernel module。
- 不要用 plain `gcc` 編 `driver_lab_debugfs_logging.c`。
- `make clean` 只清 build artifact，不做 `rmmod`。
- `KDIR` 必須對應你要 build 的 kernel。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab01 的 `.ko` 檔名是什麼？ | `driver_lab_debugfs_logging.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_debugfs_logging.o`。 |
| 為什麼要 `KDIR`？ | external module 要借用 kernel build tree。 |
| `M=$(PWD)` 的作用？ | 告訴 kbuild source 在目前 Lab01 目錄。 |
| `make clean` 會卸載 module 嗎？ | 不會。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`。<https://docs.kernel.org/kbuild/modules.html>
