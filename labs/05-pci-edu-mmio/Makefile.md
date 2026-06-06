# `Makefile` 詳解

## 結論

`labs/05-pci-edu-mmio/Makefile` 是 Lab05 的 kbuild external module 入口。它把 [`driver_lab_edu_mmio.c`](driver_lab_edu_mmio.c) 建成：

```text
driver_lab_edu_mmio.ko
```

和前面 labs 一樣，kernel module 必須透過 Linux kbuild 建置；不同的是，這個 `.ko` 的真正功能驗證需要在看得到 QEMU EDU PCI device 的 Linux guest 裡完成。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab05 driver：[`driver_lab_edu_mmio.c.md`](driver_lab_edu_mmio.c.md)。
- Lab05 test：[`test.sh.md`](test.sh.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab05 使用到的 kbuild 形狀，不展開完整 kernel Makefile 語法。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_edu_mmio.o
```

`obj-m` 告訴 kbuild：把 `driver_lab_edu_mmio.c` 編成 external module。

對照：

```text
driver_lab_edu_mmio.c
  -> driver_lab_edu_mmio.o
  -> driver_lab_edu_mmio.ko
```

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 running kernel 的 build tree。Lab05 的 `.ko` 要載入哪顆 kernel，就要用相容的 headers/build tree 建。

在 Linux guest 裡先檢查：

```sh
ls -ld /lib/modules/$(uname -r)/build
```

如果不存在，先補 kernel headers，不要先改 driver code。

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
  告訴 kbuild external module source 在 Lab05 目錄
```

概念展開：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/05-pci-edu-mmio \
  modules
```

## 四、`all`

原始碼：

```make
all:
	# 建出 driver_lab_edu_mmio.ko；真正 load/test 要在 Linux guest 內做。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

執行：

```sh
make
```

只會建出 `.ko`，不會：

- 啟動 QEMU。
- 讓 guest 看見 EDU device。
- 自動 `insmod`。
- 自動跑 liveness check。

真正功能驗證在 [`test.sh.md`](test.sh.md)。

## 五、`clean`

原始碼：

```make
clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

它會清掉 kbuild artifact，例如：

```text
driver_lab_edu_mmio.o
driver_lab_edu_mmio.ko
driver_lab_edu_mmio.mod
driver_lab_edu_mmio.mod.c
Module.symvers
modules.order
.tmp_versions/
```

重要觀念：

```text
make clean 不會 rmmod
make clean 不會 unbind PCI device
```

已載入 module 時，先：

```sh
sudo rmmod driver_lab_edu_mmio
```

再 `make clean`。

## 六、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild 目標。Lab05 一般不用安裝到系統 module tree；學習流程使用：

```sh
make
sudo insmod ./driver_lab_edu_mmio.ko
sudo rmmod driver_lab_edu_mmio
make clean
```

## 和 source/test 的對照

| Makefile target | 產物或效果 | 後續誰使用 |
|---|---|---|
| `all` | `driver_lab_edu_mmio.ko` | [`test.sh`](test.sh) 的 `insmod "./${MODULE_NAME}.ko"` |
| `clean` | 清 kbuild artifact | smoke test 收尾 |
| `modules_install` | 安裝到系統 module tree | Lab05 一般不用 |

## 常見卡點

- macOS 不能直接 build/load 這個 Linux kernel module。
- Linux guest 沒有 `/lib/modules/$(uname -r)/build` 時，先裝 kernel headers。
- build 成功不代表 `probe()` 會進來；guest 內仍要看得到 `1234:11e8`。
- `make clean` 不負責卸載 module 或釋放 PCI resource。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab05 build 產物是什麼？ | `driver_lab_edu_mmio.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_edu_mmio.o`。 |
| `make` 會讓 PCI core 呼叫 `probe()` 嗎？ | 不會；要 `insmod` 且 guest 內有 matched EDU device。 |
| `make clean` 會 unbind driver 嗎？ | 不會；要先 `rmmod`。 |
| Lab05 為什麼要在 Linux guest 驗證？ | 需要 Linux kernel module load 與 QEMU EDU PCI device。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`，`modules` / `clean` / `modules_install` target。<https://docs.kernel.org/kbuild/modules.html>
