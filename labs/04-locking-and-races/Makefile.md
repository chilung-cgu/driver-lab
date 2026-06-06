# `Makefile` 詳解

## 結論

`labs/04-locking-and-races/Makefile` 是 Lab04 的 kbuild external module 入口。它把 [`driver_lab_race.c`](driver_lab_race.c) 建成：

```text
driver_lab_race.ko
```

Lab04 的 Makefile 本身仍是標準 external module 形狀；真正比前面 labs 多的是：測試時還要另外 build userspace race CLI，因為 race reproduction 需要 pthreads。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab04 driver：[`driver_lab_race.c.md`](driver_lab_race.c.md)。
- Lab04 test：[`test.sh.md`](test.sh.md)。
- Linux kernel documentation `Building External Modules`。

這裡只解釋 Lab04 使用到的 kbuild 形狀，不展開完整 kernel Makefile 語法。

## 一、`obj-m`

原始碼：

```make
obj-m += driver_lab_race.o
```

`obj-m` 告訴 kbuild：把 `driver_lab_race.c` 編成 external module。

對照：

```text
driver_lab_race.c
  -> driver_lab_race.o
  -> driver_lab_race.ko
```

## 二、`KDIR`

原始碼：

```make
KDIR ?= /lib/modules/$(shell uname -r)/build
```

`KDIR` 指向目前 running kernel 的 build tree。external module 必須借用這個 build tree，取得相容的 kernel headers、config、module flags。

常見檢查：

```sh
ls /lib/modules/$(uname -r)/build
```

如果在 macOS 上跑，通常沒有這個路徑。Lab04 必須在 Linux 或 remote `s2` 上 build/load。

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
  告訴 kbuild external module source 在 Lab04 目錄
```

概念展開：

```sh
make -C /lib/modules/$(uname -r)/build \
  M=/path/to/driver-lab/labs/04-locking-and-races \
  modules
```

## 四、`all`

原始碼：

```make
all:
	# 建出 driver_lab_race.ko。
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

執行：

```sh
make
```

會建出：

```text
driver_lab_race.ko
```

注意：這不會 build userspace CLI。CLI 是 [`test.sh`](test.sh) 裡用 `cc -pthread` 另外建：

```sh
cc -Wall -Wextra -Werror -pthread \
  -o "$ROOT_DIR/tests/driver_lab_race_cli" \
  "$ROOT_DIR/tests/driver_lab_race_cli.c"
```

## 五、`clean`

原始碼：

```make
clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

這會清掉 kbuild artifact，例如：

```text
driver_lab_race.o
driver_lab_race.ko
driver_lab_race.mod
driver_lab_race.mod.c
Module.symvers
modules.order
.tmp_versions/
```

重要觀念：

```text
make clean 不會 rmmod
make clean 也不清 userspace CLI
```

Lab04 的 `test.sh` cleanup 會刪掉 `$ROOT_DIR/tests/driver_lab_race_cli`，這是 test script 的責任，不是 Makefile 的責任。

## 六、`modules_install`

原始碼：

```make
modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

這是標準 kbuild 目標。Lab04 一般不需要把 module 安裝到系統 module tree；學習流程用：

```sh
make
sudo insmod ./driver_lab_race.ko
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
sudo rmmod driver_lab_race
make clean
```

## 和 source/test 的對照

| Makefile target | 產物或效果 | 後續誰使用 |
|---|---|---|
| `all` | `driver_lab_race.ko` | [`test.sh`](test.sh) 的 `insmod "./${MODULE_NAME}.ko"` |
| `clean` | 清 kbuild artifact | smoke test 收尾 |
| `modules_install` | 安裝 module 到系統 module tree | Lab04 一般不用 |

## 常見卡點

- 不要用 plain `gcc` 編 kernel module。
- `make` 只建 `.ko`，不會建 `driver_lab_race_cli`。
- `driver_lab_race_cli` 需要 `-pthread`，因為它用 pthread 製造 userspace concurrency。
- `make clean` 不會卸載 module，也不會停 background kthread；必須先 `sudo rmmod driver_lab_race`。
- build 成功不代表 race 實驗成功；還要跑 [`test.sh.md`](test.sh.md)。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab04 kernel module 產物是什麼？ | `driver_lab_race.ko`。 |
| `obj-m` 放什麼？ | `driver_lab_race.o`。 |
| userspace race CLI 是 Makefile 建的嗎？ | 不是；`test.sh` 用 `cc -pthread` 另外建。 |
| `make clean` 會刪 CLI 嗎？ | 不會，CLI 由 `test.sh` cleanup 刪。 |
| `make clean` 會停 background kthread 嗎？ | 不會；要先 `rmmod`，driver exit path 才會 `kthread_stop()`。 |

## 查證來源

- Linux kernel documentation `Building External Modules`：external module 使用 `make -C <kernel_dir> M=$PWD`。<https://docs.kernel.org/kbuild/modules.html>
