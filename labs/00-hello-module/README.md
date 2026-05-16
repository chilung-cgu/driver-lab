# 00 - Hello Module

## 目的

建立最小閉環：

- build
- load
- 看 log
- unload
- clean

如果這一關做不穩，後面的 driver lab 都會浪費時間。

## 你會學到什麼

- kbuild 的最小外掛模組寫法
- `module_init` / `module_exit`
- module parameter
- `dmesg` 觀測
- `insmod` / `rmmod`

## 先備理解

先用白話記住這 3 件事：

- `insmod` 載入模組時，kernel 會呼叫 `module_init()` 指定的函式
- `rmmod` 卸載模組時，kernel 會呼叫 `module_exit()` 指定的函式
- 這不是一般 userspace 程式，所以最重要的觀測點是 `dmesg`

第一次看到 source code 時，你不需要把每個 macro 都背起來。

這一關的最低理解門檻只有：

- 你知道 module 是被 `insmod` 載入，不是用 `./program` 執行
- 你知道載入成功時會進 `driver_lab_hello_init()`
- 你知道卸載時會進 `driver_lab_hello_exit()`
- 你知道 `pr_info()` 印出的文字要去 `dmesg` 看

其他像 `MODULE_LICENSE()`、`MODULE_AUTHOR()`、`MODULE_DESCRIPTION()`，第一次先當成 module metadata。它們很重要，但不是 Chapter 0 的主要卡點。

## 你現在在系統的哪一層

這一關還沒有 `/dev`、沒有 user-kernel data path，也沒有硬體。

你只是在練：

- kernel module 能不能被正確 build
- module 能不能被載入
- module 的 init / exit path 能不能被觀測

## 檔案

- `driver_lab_hello.c`
- `Makefile`
- `test.sh`
- `quality.sh`

## 使用方式

```sh
make
modinfo ./driver_lab_hello.ko
sudo insmod ./driver_lab_hello.ko who=linux repeat=2
lsmod | grep '^driver_lab_hello'
sudo dmesg | tail -n 20
sudo rmmod driver_lab_hello
make clean
```

命令逐行在做什麼：

- `make`：用目前目錄的 `Makefile` 建出 `driver_lab_hello.ko`
- `modinfo`：先看這個 module 的基本資訊，確認檔案有被正確產生
- `insmod ... who=linux repeat=2`：把 module 載入 kernel，並帶入參數
- `lsmod | grep ...`：確認 module 真的在 kernel 裡
- `dmesg | tail ...`：看剛剛載入時印出的 kernel log
- `rmmod`：卸載 module
- `make clean`：刪掉建置產物

## 執行流程

這支 module 沒有 `main()`。

它的流程是：

```text
make
  -> 產生 driver_lab_hello.ko

sudo insmod ./driver_lab_hello.ko who=linux repeat=2
  -> kernel 載入 module
  -> kernel 呼叫 driver_lab_hello_init()
  -> pr_info() 把訊息放進 kernel log

sudo dmesg | tail -n 20
  -> 從 userspace 觀察剛剛的 kernel log

sudo rmmod driver_lab_hello
  -> kernel 呼叫 driver_lab_hello_exit()
  -> module 從 kernel 移除
```

所以你讀 code 時不要找 `main()`。kernel module 的入口是 `module_init()` 指定的函式。

## 自動化 smoke test

```sh
./test.sh
```

## 驗收標準

- `make` 成功
- `insmod` 成功
- kernel log 出現 `driver_lab_hello`
- `rmmod` 成功
- `make clean` 成功

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| module 的載入入口在哪裡？ | `insmod` 成功後，kernel 會呼叫 `module_init()` 登記的 `driver_lab_hello_init()`。 |
| module 的卸載入口在哪裡？ | `rmmod driver_lab_hello` 時，kernel 會呼叫 `module_exit()` 登記的 `driver_lab_hello_exit()`。 |
| 第一個觀測點是什麼？ | `sudo dmesg | tail` 裡的 `driver_lab_hello` log。 |
| 這一關主要拿到什麼 resource？ | 這一關沒有建立 `/dev`、debugfs 或硬體 resource；主要只是把 `.ko` 載入 kernel。 |
| cleanup 做了什麼？ | `driver_lab_hello_exit()` 只印出卸載 log；真正把 module 從 kernel 移除的是 `rmmod` 流程。 |
| `insmod` 失敗時第一個看哪裡？ | 先看 `sudo dmesg | tail -n 50`，再檢查 `/lib/modules/$(uname -r)/build` 與 Secure Boot / module signature。 |

## 常見失敗

- `/lib/modules/$(uname -r)/build` 不存在
- Secure Boot / module signing 導致載入失敗
- 不是在 Linux 主機上執行

## 看不懂 code 時，至少先找到這 3 個位置

- `driver_lab_hello_init()`：模組載入時做什麼
- `driver_lab_hello_exit()`：模組卸載時做什麼
- `module_param()`：參數是怎麼進來的

## Chapter 0 符號速查

| 符號 | 現在先怎麼理解 | Chapter 0 需要深入嗎 |
|---|---|---|
| `pr_fmt(fmt)` | 幫這個檔案的 `pr_info()` log 自動加上 module 名稱前綴 | 不需要，知道它讓 `dmesg` 比較好讀即可 |
| `#include <linux/...>` | kernel module 用的 header，不是一般 C library header | 不需要逐個背 |
| `module_param(who, charp, 0444)` | 宣告 `who` 可以從 `insmod ... who=...` 傳進來 | 需要知道用途 |
| `MODULE_PARM_DESC()` | 參數說明，會讓 `modinfo` 顯示較清楚 | 暫時當 metadata |
| `static int __init driver_lab_hello_init(void)` | module 載入時要跑的函式 | 需要知道這是載入入口 |
| `static void __exit driver_lab_hello_exit(void)` | module 卸載時要跑的函式 | 需要知道這是卸載入口 |
| `pr_info()` | kernel log 的 info 等級輸出，通常用 `dmesg` 看 | 需要知道用途 |
| `module_init()` | 告訴 kernel 載入 module 時呼叫哪個函式 | 需要知道用途 |
| `module_exit()` | 告訴 kernel 卸載 module 時呼叫哪個函式 | 需要知道用途 |
| `MODULE_LICENSE()` | 告訴 kernel module loader 這個 module 的 license 類型，也會影響 taint / GPL-only symbol 判斷 | 先知道它不是裝飾品即可 |
| `MODULE_AUTHOR()` | module 作者資訊，`modinfo` 可看到 | 暫時當 metadata |
| `MODULE_DESCRIPTION()` | module 說明，`modinfo` 可看到 | 暫時當 metadata |
| `-EINVAL` | 回傳錯誤碼，代表參數不合法 | 知道它會讓 `insmod` 失敗即可 |

## 你現在不需要卡住的地方

- 不需要理解 `__init` / `__exit` 背後的 section 管理細節。
- 不需要理解 `0444` 的所有權限位元，只要知道這裡表示參數可被讀取。
- 不需要背 `MODULE_*` 巨集的實作。
- 不需要知道 kernel log ring buffer 的完整內部結構。

這一關要先練的是「能 build、能 load、能觀察、能 unload」。如果這個閉環能穩定跑通，後面再回頭補細節比較有效。

## 下一步

完成後進入 [`../01-debugfs-logging`](../01-debugfs-logging)
