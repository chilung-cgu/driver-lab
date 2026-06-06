# `Makefile` 詳解

## 結論

`labs/06-pci-edu-irq/Makefile` 是 Lab06 的 kbuild external module 入口。它把 [`driver_lab_edu_irq.c`](driver_lab_edu_irq.c) 建成：

```text
driver_lab_edu_irq.ko
```

和 Lab05 一樣，這個 Makefile 只負責 build/clean/install module artifact；它不會啟動 QEMU、不會讓 guest 看見 EDU、不會自動測 IRQ。真正功能驗證在 [`test.sh.md`](test.sh.md)。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab06 driver：[`driver_lab_edu_irq.c.md`](driver_lab_edu_irq.c.md)。
- Lab06 test：[`test.sh.md`](test.sh.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab06 使用的 external module kbuild 形狀，不展開完整 kernel build system。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_edu_irq.o
```

`obj-m` 告訴 kbuild：把 `driver_lab_edu_irq.c` 編成 external module。

對照：

```text
driver_lab_edu_irq.c
  -> driver_lab_edu_irq.o
  -> driver_lab_edu_irq.ko
```

不要用一般：

```sh
gcc driver_lab_edu_irq.c
```

kernel module 需要 kernel headers、kbuild flags、module metadata 和 vermagic，必須交給 kernel kbuild。

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 running kernel 的 build tree。

Lab06 的 `.ko` 要載入哪顆 kernel，就要用相容的 headers/build tree 建。進 Linux guest 後先檢查：

```sh
ls -ld /lib/modules/$(uname -r)/build
```

如果不存在，先補 kernel headers，不要先改 Lab06 source。

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
  告訴 kbuild external module source 在 Lab06 目錄
```

展開後概念上像：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/06-pci-edu-irq \
  modules
```

## 四、`all`

原始碼：

```make
all:
	# 建出 driver_lab_edu_irq.ko；真正 load/test 要在 Linux guest 內做。
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
- `request_irq()`。
- 觸發 IRQ self-test。

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
driver_lab_edu_irq.o
driver_lab_edu_irq.ko
driver_lab_edu_irq.mod
driver_lab_edu_irq.mod.c
Module.symvers
modules.order
.tmp_versions/
```

重要觀念：

```text
make clean 不會 rmmod
make clean 不會 free_irq
make clean 不會 unbind PCI device
```

如果 module 還載著，先：

```sh
sudo rmmod driver_lab_edu_irq
```

再 `make clean`。

## 六、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild target。Lab06 一般不用把 module 安裝到系統 module tree；學習流程是：

```sh
make
sudo insmod ./driver_lab_edu_irq.ko
sudo rmmod driver_lab_edu_irq
make clean
```

或直接：

```sh
./test.sh
```

## 和 source/test 的對照

| Makefile target | 產物或效果 | 後續誰使用 |
|---|---|---|
| `all` | `driver_lab_edu_irq.ko` | [`test.sh`](test.sh) 的 `insmod "./${MODULE_NAME}.ko"` |
| `clean` | 清 kbuild artifact | smoke test 收尾 |
| `modules_install` | 安裝到系統 module tree | Lab06 一般不用 |

## 常見卡點

- macOS 不能直接 build/load Lab06 Linux kernel module。
- Linux guest 沒有 `/lib/modules/$(uname -r)/build` 時，先裝 kernel headers。
- build 成功不代表 IRQ 成功；還要 guest 內看得到 `1234:11e8`，並且 `test.sh` 能跑到 `irq self-test passed`。
- `make clean` 不負責卸載 module 或釋放 IRQ handler。
- 如果 `insmod` 失敗，看 `dmesg`，不要只看 `make` output。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab06 build 產物是什麼？ | `driver_lab_edu_irq.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_edu_irq.o`。 |
| `make` 會註冊 IRQ handler 嗎？ | 不會；要 `insmod` 後 `probe()` 成功進到 `request_irq()`。 |
| `make clean` 會 `free_irq()` 嗎？ | 不會；`free_irq()` 發生在 driver error path 或 remove path。 |
| Lab06 為什麼要在 Linux guest 驗證？ | 需要 Linux kernel module load、QEMU EDU PCI device、IRQ delivery 與 `/proc/interrupts`。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`，`modules` / `clean` / `modules_install` target。<https://docs.kernel.org/kbuild/modules.html>
