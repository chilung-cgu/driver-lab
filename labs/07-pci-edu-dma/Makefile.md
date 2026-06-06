# `Makefile` 詳解

## 結論

`labs/07-pci-edu-dma/Makefile` 是 Lab07 的 kbuild external module 入口。它把 [`driver_lab_edu_dma.c`](driver_lab_edu_dma.c) 建成：

```text
driver_lab_edu_dma.ko
```

這個 Makefile 只負責 build/clean/install module artifact；它不會啟動 QEMU、不會建立 EDU PCI device、不會自動配置 DMA buffer，也不會跑 round-trip。真正功能驗證在 [`test.sh.md`](test.sh.md)。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab07 driver：[`driver_lab_edu_dma.c.md`](driver_lab_edu_dma.c.md)。
- Lab07 test：[`test.sh.md`](test.sh.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab07 使用的 external module kbuild 形狀，不展開完整 kernel build system。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_edu_dma.o
```

`obj-m` 告訴 kbuild：把 `driver_lab_edu_dma.c` 編成 external module。

對照：

```text
driver_lab_edu_dma.c
  -> driver_lab_edu_dma.o
  -> driver_lab_edu_dma.ko
```

不要用一般：

```sh
gcc driver_lab_edu_dma.c
```

kernel module 需要 kernel headers、kbuild flags、module metadata、vermagic，以及 kernel build system 提供的正確 include path。

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 running kernel 的 build tree。

Lab07 的 `.ko` 要載入哪顆 kernel，就要用相容的 headers/build tree 建。進 Linux guest 後先檢查：

```sh
ls -ld /lib/modules/$(uname -r)/build
```

如果不存在，先補 kernel headers，不要先改 Lab07 source。

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
  告訴 kbuild external module source 在 Lab07 目錄
```

展開後概念上像：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/07-pci-edu-dma \
  modules
```

## 四、`all`

原始碼：

```make
all:
	# 建出 driver_lab_edu_dma.ko；真正 load/test 要在 Linux guest 內做。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

執行：

```sh
make
```

只會建出 `.ko`，不會：

- 啟動 QEMU。
- 建立 EDU PCI device。
- `insmod` module。
- 設定 DMA mask。
- 配置 coherent DMA buffer。
- 觸發 DMA round-trip。

這些都發生在 Linux guest 執行 [`test.sh`](test.sh) 時。

## 五、`clean`

原始碼：

```make
clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

它會清掉 kbuild artifact，例如：

```text
driver_lab_edu_dma.o
driver_lab_edu_dma.ko
driver_lab_edu_dma.mod
driver_lab_edu_dma.mod.c
Module.symvers
modules.order
.tmp_versions/
```

重要觀念：

```text
make clean 不會 rmmod
make clean 不會 free_irq
make clean 不會 dma_free_coherent
make clean 不會 unbind PCI device
```

如果 module 還載著，先：

```sh
sudo rmmod driver_lab_edu_dma
```

再 `make clean`。

## 六、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild target。Lab07 一般不用把 module 安裝到系統 module tree；學習流程是：

```sh
make
sudo insmod ./driver_lab_edu_dma.ko
sudo rmmod driver_lab_edu_dma
make clean
```

或直接：

```sh
./test.sh
```

## 和 source/test 的對照

| Makefile target | 產物或效果 | 後續誰使用 |
|---|---|---|
| `all` | `driver_lab_edu_dma.ko` | [`test.sh`](test.sh) 的 `insmod "./${MODULE_NAME}.ko"` |
| `clean` | 清 kbuild artifact | smoke test 收尾 |
| `modules_install` | 安裝到系統 module tree | Lab07 一般不用 |

## 常見卡點

- macOS 不能直接 build/load Lab07 Linux kernel module。
- Linux guest 沒有 `/lib/modules/$(uname -r)/build` 時，先裝 kernel headers。
- build 成功不代表 DMA 成功；還要 guest 內看得到 `1234:11e8`，並且 `test.sh` 能跑到 `round-trip compare passed`。
- `make clean` 不負責卸載 module、釋放 IRQ handler 或釋放 coherent DMA buffer。
- 如果 `insmod` 失敗，看 `dmesg`，不要只看 `make` output。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab07 build 產物是什麼？ | `driver_lab_edu_dma.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_edu_dma.o`。 |
| `make` 會配置 DMA buffer 嗎？ | 不會；要 `insmod` 後 `probe()` 成功走到 `dma_alloc_coherent()`。 |
| `make clean` 會呼叫 `dma_free_coherent()` 嗎？ | 不會；那是 driver error/remove path 的責任。 |
| Lab07 為什麼要在 Linux guest 驗證？ | 需要 Linux kernel module load、QEMU EDU PCI device、IRQ delivery、DMA API 和 EDU DMA engine。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`，`modules` / `clean` / `modules_install` target。<https://docs.kernel.org/kbuild/modules.html>
