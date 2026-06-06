# `driver_lab_hello.c` 詳解

## 結論

`labs/00-hello-module/driver_lab_hello.c` 是整個 driver-lab 的第一個 kernel module。它不建立 `/dev`，不碰硬體，也沒有 userspace data path；它只建立最小閉環：

```text
make
  -> driver_lab_hello.ko
sudo insmod
  -> driver_lab_hello_init()
  -> pr_info() 寫 kernel log
sudo dmesg
  -> 從 userspace 觀察 kernel log
sudo rmmod
  -> driver_lab_hello_exit()
make clean
```

這一關的目的不是寫出「有功能的 driver」，而是確認你能穩定做到 build、load、observe、unload、clean。後面所有 lab 都建立在這個閉環上。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh`](test.sh)、[`Makefile`](Makefile)。
- Linux kernel documentation 的 `module_init()` / `module_exit()`、printk / `pr_info()`、external module build。
- Linux man-pages 的 `insmod(8)` 和 `dmesg(1)`。

這裡不展開 `__init` / `__exit` 背後的 linker section 細節，也不討論 module signing / Secure Boot 的完整處理流程；那些先放在 debug checklist。

## 先理解這份檔案在 repo 的位置

Lab00 只有三個主要 source/build/test 檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_hello.c`](driver_lab_hello.c) | kernel module source |
| [`Makefile.md`](Makefile.md) | 用 kbuild 建 `.ko` |
| [`test.sh.md`](test.sh.md) | 自動跑 build/load/log/unload/clean |

這一關還沒有 runtime、CLI、UAPI header，也沒有 `/dev` node。

## 這份檔案要解決什麼問題？

很多新手一開始會用 userspace C 程式的習慣找：

```c
int main(...)
```

但 kernel module 沒有 `main()`。它的入口由 macro 登記：

```c
module_init(driver_lab_hello_init);
module_exit(driver_lab_hello_exit);
```

白話講：

```text
insmod 成功時，kernel 呼叫 init function
rmmod 成功時，kernel 呼叫 exit function
觀察結果不是看 stdout，而是看 dmesg
```

## 一、`pr_fmt`：讓 kernel log 帶 module 前綴

原始碼：

```c
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
```

`pr_info()` 這類 logging macro 會使用 `pr_fmt()`。這裡把 module 名稱加到 log 前面，讓你在 `dmesg` 裡容易 grep：

```text
driver_lab_hello: init who=smoke-test repeat=2
driver_lab_hello: hello 1/2 to smoke-test
driver_lab_hello: exit
```

白話講：

```text
pr_fmt 不是主要 driver 邏輯
它是讓 log 比較容易讀
```

## 二、kernel header

原始碼：

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
```

這不是 userspace C library。kernel module 用的是 kernel headers。

| header | 用途 |
|---|---|
| `<linux/init.h>` | `__init` / `__exit` |
| `<linux/module.h>` | `module_init()`、`module_exit()`、`MODULE_*` metadata |
| `<linux/moduleparam.h>` | `module_param()`、`MODULE_PARM_DESC()` |

## 三、module parameter：`who`

原始碼：

```c
static char *who = "driver-lab";
module_param(who, charp, 0444);
MODULE_PARM_DESC(who, "Greeting target shown in kernel log");
```

`who` 是 module parameter。你可以在 `insmod` 時傳入：

```sh
sudo insmod ./driver_lab_hello.ko who=linux
```

如果不傳，預設是：

```text
driver-lab
```

`charp` 代表字串指標。`0444` 表示載入後這個參數可被讀取。

白話講：

```text
who 是使用者載入 module 時給 kernel module 的一個小設定
```

## 四、module parameter：`repeat`

原始碼：

```c
static int repeat = 1;
module_param(repeat, int, 0444);
MODULE_PARM_DESC(repeat, "How many hello messages to emit (1-8)");
```

`repeat` 決定 init 時印幾行 hello。

使用方式：

```sh
sudo insmod ./driver_lab_hello.ko who=linux repeat=2
```

這個值會在 init function 裡被檢查：

```c
if (repeat < 1 || repeat > 8)
	return -EINVAL;
```

如果傳 `repeat=0` 或 `repeat=99`，`insmod` 會失敗，因為 init function 回傳錯誤。

## 五、`driver_lab_hello_init()`：module 載入入口

原始碼：

```c
static int __init driver_lab_hello_init(void)
{
	int i;

	if (repeat < 1 || repeat > 8)
		return -EINVAL;

	pr_info("init who=%s repeat=%d\n", who, repeat);

	for (i = 0; i < repeat; ++i)
		pr_info("hello %d/%d to %s\n", i + 1, repeat, who);

	return 0;
}
```

這個 function 是 module 載入時的核心。

流程：

1. 檢查 `repeat` 合法範圍。
2. 印 init log。
3. 印 `repeat` 次 hello log。
4. 回 0 代表載入成功。

如果回 `-EINVAL`，kernel 會視為 module init 失敗，`insmod` 會失敗。

白話講：

```text
init function 成功回 0
失敗回負 errno
這決定 module 是否真的被載入 kernel
```

## 六、`driver_lab_hello_exit()`：module 卸載入口

原始碼：

```c
static void __exit driver_lab_hello_exit(void)
{
	pr_info("exit\n");
}
```

這是 `rmmod` 時會呼叫的 cleanup function。

Lab00 沒有配置 memory、沒有註冊 char device、沒有建立 debugfs，所以 exit 只需要印 log。

白話講：

```text
這一關沒有 resource 要釋放
exit log 只是讓你確認 rmmod 真的走到 module_exit path
```

## 七、`module_init()` / `module_exit()`

原始碼：

```c
module_init(driver_lab_hello_init);
module_exit(driver_lab_hello_exit);
```

官方 kernel docs 對 `module_init()` 的定位是 driver initialization entry point；module 載入時會呼叫它登記的 function。`module_exit()` 則登記 driver removed 時要跑的 cleanup function。

這兩行是 kernel module 的入口登記，不是普通 function call。

白話講：

```text
你沒有在 source 裡看到 main()
因為 kernel 會根據 module_init/module_exit 呼叫你的 function
```

## 八、MODULE metadata

原始碼：

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 0 hello kernel module for driver-lab");
```

這些可以用 `modinfo` 看到。

`MODULE_LICENSE("GPL")` 不只是註解。kernel module loader 會根據 license 判斷 module license 類型，也會影響 taint 與 GPL-only symbol 使用。

第一輪先這樣記：

```text
MODULE_LICENSE 很重要
MODULE_AUTHOR / MODULE_DESCRIPTION 主要是 metadata
```

## source 和命令的對照

| 命令 | source path |
|---|---|
| `make` | 產生 `driver_lab_hello.ko` |
| `sudo insmod ./driver_lab_hello.ko who=linux repeat=2` | kernel 呼叫 `driver_lab_hello_init()` |
| `sudo dmesg | tail` | 看到 `pr_info()` log |
| `sudo rmmod driver_lab_hello` | kernel 呼叫 `driver_lab_hello_exit()` |
| `modinfo ./driver_lab_hello.ko` | 看到 `MODULE_*` metadata 與 parameter description |

## 常見卡點

- kernel module 沒有 `main()`。
- `pr_info()` 不是印到 terminal stdout，要用 `dmesg` 看。
- `repeat` 超出 `1-8` 會讓 `insmod` 失敗。
- `MODULE_PARM_DESC()` 是描述，不負責驗證參數。
- `__init` / `__exit` 第一輪不用深挖 section 細節，先知道它們標示 init/exit 用途。
- 這一關沒有 `/dev` node；不要期待看到 `/dev/driver_lab_*`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這份 module 的載入入口是哪個 function？ | `driver_lab_hello_init()`。 |
| 這份 module 的卸載入口是哪個 function？ | `driver_lab_hello_exit()`。 |
| `who` 和 `repeat` 怎麼傳入？ | 透過 `insmod ./driver_lab_hello.ko who=... repeat=...`。 |
| `repeat=99` 會怎樣？ | init 回 `-EINVAL`，`insmod` 失敗。 |
| `pr_info()` 的輸出去哪裡看？ | `dmesg`。 |
| 這一關有建立 `/dev` node 嗎？ | 沒有。 |

## 查證來源

- Linux kernel documentation `Driver Basics`：`module_init()` / `module_exit()`。<https://docs.kernel.org/driver-api/basics.html>
- Linux kernel documentation `Message logging with printk`：`pr_info()` 與 log level。<https://docs.kernel.org/core-api/printk-basics.html>
- Linux man-pages `insmod(8)`：`insmod` 載入 module，錯誤時通常可看 `dmesg`。<https://man7.org/linux/man-pages/man8/insmod.8.html>
- Linux man-pages `dmesg(1)`：讀 kernel ring buffer。<https://man7.org/linux/man-pages/man1/dmesg.1.html>
