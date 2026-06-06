# `Makefile` 詳解

## 結論

`labs/02-char-device/Makefile` 是 Lab02 的 kbuild external module 入口。它把 [`driver_lab_char.c`](driver_lab_char.c) 交給目前 Linux kernel build tree，產生：

```text
driver_lab_char.ko
```

這份 Makefile 和 Lab00/Lab01 很像，但這次建出的 module 會建立 `/dev/driver_lab_char0`，所以 build 成功只是第一步；真正驗收還要在 Linux 上 `insmod` 並跑 [`test.sh.md`](test.sh.md)。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab02 driver：[`driver_lab_char.c.md`](driver_lab_char.c.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab02 使用到的 kbuild 形狀，不展開完整 kernel Makefile 語法。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_char.o
```

`obj-m` 告訴 kbuild：這個目錄要建一個 external module。

對照：

```text
driver_lab_char.c
  -> driver_lab_char.o
  -> driver_lab_char.ko
```

你不需要手動指定：

```sh
gcc -c driver_lab_char.c
```

kernel module 必須用 kbuild，因為它需要目前 kernel 的 headers、config、module metadata 與編譯 flags。

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 running kernel 對應的 build tree。常見展開結果像：

```text
/lib/modules/6.x.y/build
```

`?=` 表示 caller 可以覆蓋：

```sh
make KDIR=/path/to/kernel/build
```

常見失敗原因：

| 錯誤現象 | 常見原因 |
|---|---|
| `/lib/modules/.../build` 不存在 | 沒安裝 kernel headers。 |
| 在 macOS 跑 `make` | macOS 沒有 Linux kernel build tree。 |
| build 成功但 `insmod` 失敗 | headers/build tree 可能和 running kernel 不匹配，或 module signing/Secure Boot 等環境限制。 |

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
  告訴 kbuild external module source 在 Lab02 目錄
```

概念展開：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/02-char-device \
  modules
```

這是 Linux kernel documentation 裡 external module build 的標準形狀。

## 四、`all`

原始碼：

```make
all:
	# 建出 driver_lab_char.ko。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

在 Lab02 目錄執行：

```sh
make
```

會跑 `all`，最後產生：

```text
driver_lab_char.ko
```

注意：`make` 只負責 build，不會載入 module，也不會建立 `/dev/driver_lab_char0`。`/dev` node 要等：

```sh
sudo insmod ./driver_lab_char.ko
```

成功後才會由 driver init path 和 device model/devtmpfs 產生。

## 五、`clean`

原始碼：

```make
clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

它會清掉 kbuild artifact，例如：

```text
driver_lab_char.o
driver_lab_char.ko
driver_lab_char.mod
driver_lab_char.mod.c
Module.symvers
modules.order
.tmp_versions/
```

重要觀念：

```text
make clean 不會 rmmod
```

如果 module 已經載入 kernel，要先：

```sh
sudo rmmod driver_lab_char
```

再 `make clean`。

## 六、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild 目標，用來把 module 安裝到系統 module tree。Lab02 一般不需要執行，因為學習流程是：

```sh
make
sudo insmod ./driver_lab_char.ko
./test.sh
sudo rmmod driver_lab_char
make clean
```

## 和 source/test 的對照

| Makefile target | 產物或效果 | 後續誰使用 |
|---|---|---|
| `all` | `driver_lab_char.ko` | [`test.sh`](test.sh) 的 `insmod ./driver_lab_char.ko` |
| `clean` | 清掉 kbuild artifact | 測試收尾或手動清理 |
| `modules_install` | 安裝到系統 module tree | 一般 Lab02 學習流程不用 |

## 常見卡點

- 不要用 plain `gcc` 編 kernel module。
- macOS 本機不能 build/load 這個 Linux kernel module；要在 Linux 或 remote `s2` 上跑。
- `make clean` 只清檔案，不會卸載 kernel 裡已經載入的 module。
- `KDIR` 必須對應要載入的 running kernel。
- build 出 `.ko` 不代表 `/dev/driver_lab_char0` 會出現；那是 `insmod` 後 init path 的結果。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab02 build 產物是什麼？ | `driver_lab_char.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_char.o`。 |
| `KDIR` 預設指向哪裡？ | `/lib/modules/$(uname -r)/build`。 |
| `M=$(PWD)` 的作用？ | 告訴 kbuild external module source 在目前 Lab02 目錄。 |
| `make` 會建立 `/dev/driver_lab_char0` 嗎？ | 不會；要 `insmod` 成功後才會建立 device surface。 |
| `make clean` 會卸載 module 嗎？ | 不會。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`。<https://docs.kernel.org/kbuild/modules.html>
