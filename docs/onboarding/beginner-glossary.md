# 新手術語表

## 這份表怎麼用

你不需要一次背完。

遇到看不懂的字時再回來查，夠了。

## `kernel module`

意思：

- 可以動態載入到 Linux kernel 的程式碼

你現在先把它想成：

- 不用重編整顆 kernel
- 也不用重開機
- 可以用 `insmod` 載入、`rmmod` 卸載的一小塊 kernel 程式

## `.ko`

意思：

- kernel object
- Linux kernel module 編譯後常見的檔案副檔名

例如：

- `driver_lab_hello.ko`

## `KDIR`

意思：

- 常見變數名，代表 `kernel directory`

在這個專案脈絡下，通常是：

```sh
/lib/modules/"$(uname -r)"/build
```

它不是固定 magic name，只是業界很常這樣寫。

## `build tree`

意思：

- 給目前這顆 kernel 用的 build 環境

你可以先把它想成：

- kernel headers
- 對應的 Makefile / kbuild 基礎設施
- 建置 module 需要的設定與檔案

## `kbuild`

意思：

- Linux kernel 的建置系統

對你現在最重要的意義：

- 外掛 module 不是自己亂寫 gcc 指令編譯
- 要透過 kbuild 來吃到正確的 flags 與 kernel build 規則

## `module_init()`

意思：

- 告訴 kernel：這個 module 載入時要先呼叫哪個函式

在 `00-hello-module` 裡：

- `module_init(driver_lab_hello_init)` 代表 `insmod` 成功載入時會進 `driver_lab_hello_init()`

你現在先記：

- kernel module 沒有 `main()`
- `module_init()` 指定的函式就是第一個入口

## `module_exit()`

意思：

- 告訴 kernel：這個 module 卸載時要呼叫哪個 cleanup 函式

在 `00-hello-module` 裡：

- `module_exit(driver_lab_hello_exit)` 代表 `rmmod` 時會進 `driver_lab_hello_exit()`

你現在先記：

- init 拿到的 resource，通常要在 exit 對稱釋放

## `module_param()`

意思：

- 宣告一個 module parameter，讓你載入 module 時可以傳值

例如：

```sh
sudo insmod ./driver_lab_hello.ko who=linux repeat=2
```

對應到 code：

- `module_param(who, charp, 0444)`
- `module_param(repeat, int, 0444)`

你現在先記：

- `who` / `repeat` 不是 shell 變數
- 它們是 kernel module 載入時解析出的參數

## `MODULE_PARM_DESC()`

意思：

- 替 module parameter 加上說明文字

你可以先把它想成：

- 給 `modinfo` 與閱讀者看的參數文件
- 不負責實際驗證參數值是否合法

## `MODULE_LICENSE()`

意思：

- 告訴 kernel module loader 這個 module 宣告的 license 類型

它不是單純裝飾文字，因為它會影響：

- kernel 是否把 module 視為 GPL-compatible
- 載入後 kernel taint 狀態
- 是否能使用某些 `EXPORT_SYMBOL_GPL()` 匯出的 symbol

Chapter 0 你不用深入 license 法律細節，但要知道：

- loadable kernel module 通常需要 `MODULE_LICENSE()`
- 少了或寫錯，debug 時可能看到 taint 相關訊息

## `MODULE_AUTHOR()` / `MODULE_DESCRIPTION()`

意思：

- module metadata

你可以用：

```sh
modinfo ./driver_lab_hello.ko
```

觀察它們如何顯示。

第一次先把它們當成：

- 給人與工具看的描述資訊

## `insmod`

意思：

- 把 `.ko` 載入到 kernel

## `rmmod`

意思：

- 把已載入的 module 從 kernel 卸載

## `modprobe`

意思：

- 載入 module 的較高階工具

和 `insmod` 的差異：

- `insmod` 比較直接
- `modprobe` 會考慮 module 依賴與系統 module 配置

這個專案前期常先用 `insmod`，因為比較直接、比較好理解。

## `dmesg`

意思：

- 看 kernel ring buffer log 的常用指令

你可以先把它想成：

- kernel 世界的第一個觀測窗

## `pr_info()`

意思：

- kernel 裡常用的 info 等級 log macro

在這個專案裡：

- `pr_info("hello\n")` 印出的內容通常用 `dmesg` 看

你現在先記：

- 它不是 `printf()`
- 它不是印到你的 terminal stdout
- 它是把訊息送進 kernel log

## `pr_debug()`

意思：

- debug 等級 log macro

和 `pr_info()` 的差異：

- `pr_info()` 通常會直接出現在 kernel log
- `pr_debug()` 常搭配 dynamic debug，需要打開後才看得到

在 `01-debugfs-logging` 會開始碰到它。

## `pr_fmt()`

意思：

- 給 `pr_info()`、`pr_debug()` 這類 `pr_*()` macro 用的格式前綴

常見寫法：

```c
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
```

你現在先記：

- 它讓 log 自動帶 module 名稱
- 目的是讓 `dmesg` 裡的訊息比較好追

## `debugfs`

意思：

- kernel 提供給開發者使用的 debug 檔案系統

適合放：

- counter
- 狀態
- debug 控制開關

不適合：

- 正式產品 ABI

## `taint`

意思：

- kernel 是否被標記成「曾發生某些會影響 debug 信任度的事件」

對新手先記：

- `0`：目前乾淨
- 非 `0`：代表曾發生過某些值得注意的事件

## `__init`

意思：

- 給 kernel 的 annotation，表示這段函式主要用在初始化階段

在外掛 module 的學習初期，你可以先把它理解成：

- `driver_lab_hello_init()` 是初始化入口
- `__init` 是額外給 kernel build/runtime 使用的標記

你現在不需要深入 section 管理。

## `__exit`

意思：

- 給 kernel 的 annotation，表示這段函式主要用在卸載/退出階段

在外掛 module 的學習初期，你可以先把它理解成：

- `driver_lab_hello_exit()` 是卸載入口
- `__exit` 是額外給 kernel build/runtime 使用的標記

你現在不需要深入 built-in driver 與 loadable module 的差異。

## `errno` / `-EINVAL` / `-EFAULT`

意思：

- Linux kernel 常用負數錯誤碼回報失敗原因

例子：

- `-EINVAL`：invalid argument，參數不合法
- `-EFAULT`：通常代表 user pointer 複製或存取失敗
- `-ENOMEM`：記憶體配置失敗

你現在先記：

- kernel function 常用 `0` 表示成功
- 用負數 errno 表示失敗
- `insmod` 或 userspace syscall 會把這些錯誤轉成你看得到的失敗訊息

## `THIS_MODULE`

意思：

- 指向目前這個 kernel module 的物件

常見位置：

- `file_operations` 裡的 `.owner = THIS_MODULE`

你現在先記：

- 它讓 kernel 知道這些 callback 屬於哪個 module
- 這和 module lifetime 管理有關，避免 module 還被使用時被不安全卸載

## `userspace`

意思：

- 一般應用程式所在的空間

例如：

- shell
- 你的 CLI 測試程式
- `cat`、`dd`、`tee`

## `kernel space`

意思：

- Linux kernel 本身運作的空間

driver module 載入後就是在這裡跑。

## `/dev/xxx`

意思：

- device node

對你現在要記的核心觀念：

- 它看起來像檔案
- 但你對它做的 `read()` / `write()`，最後可能會進到 driver callback

## `VFS`

意思：

- Virtual File System

你現在不用深究完整架構，只要知道：

- 它是 userspace 檔案操作進入 driver 的中間層

## `file_operations`

意思：

- driver 提供給 VFS 的 callback 集合

常見成員：

- `.open`
- `.read`
- `.write`
- `.release`

## `ABI`

意思：

- Application Binary Interface

在這個專案裡，可先粗略理解成：

- userspace 跟 driver 之間約定好的介面行為

例如：

- `read/write` 怎麼用
- `ioctl` command number 是什麼
- `poll` 等什麼事件
- `mmap` 映射哪塊 buffer
