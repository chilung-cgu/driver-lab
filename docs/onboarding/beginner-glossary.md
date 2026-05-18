# 新手術語表

## 這份表怎麼用

你不需要一次背完。

遇到看不懂的字時再回來查，夠了。

這份檔案刻意保留成單一查詢表，避免你要在很多小檔案之間找名詞。大致分段如下：

| 區段 | 內容 |
|---|---|
| 基礎 module | `.ko`、kbuild、`module_init()`、`insmod`、`dmesg` |
| 觀測與 debugfs | `pr_info()`、`pr_debug()`、dynamic debug、debugfs、`seq_file` |
| user-kernel 邊界 | `/dev`、VFS、`file_operations`、`copy_to_user()`、`ioctl`、`poll`、`mmap` |
| 併發 | mutex、race、lost update、kthread、completion |
| PCI / IRQ / DMA | PCI ID、`probe()`、BAR、MMIO、IRQ、MSI、DMA、coherent buffer |
| runtime / 驗證 | runtime、ABI/API、smoke、stress、regression、fault injection、KUnit、kselftest |

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

## `dynamic debug`

意思：

- Linux kernel 提供的一套 runtime debug log 開關機制

你現在先記：

- `pr_debug()` 不一定預設出現在 `dmesg`
- dynamic debug 可以選擇只打開某個 module、某個檔案、某個函式或某一行的 debug log
- `01-debugfs-logging` 第一輪只用到「打開某個 module」這種最簡單形式

例子：

```sh
echo 'module driver_lab_debugfs_logging +p' | sudo tee /proc/dynamic_debug/control
```

這代表：

- 找到 module 名稱是 `driver_lab_debugfs_logging` 的 debug callsites
- 加上 `p` flag，讓這些 `pr_debug()` 可以被印出

## `/proc/dynamic_debug/control`

意思：

- dynamic debug 的控制檔

你現在先記：

- 如果這個檔案存在，代表目前 kernel 支援 dynamic debug 控制介面
- 寫入像 `module xxx +p` 的命令，可以打開特定 debug log
- 如果不存在，`01` 的 test 會跳過 dynamic debug 那段，不應直接判定 driver 壞掉

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

## `debugfs_create_file()`

意思：

- 在 debugfs 裡建立一個由 driver callback 支援的檔案

在 `01-debugfs-logging` 裡：

```c
debugfs_create_file("trigger", 0200, dl_root, NULL, &dl_trigger_fops);
```

你現在先記：

- `"trigger"` 是檔名
- `0200` 表示這個檔案主要給 root 寫入
- `&dl_trigger_fops` 是 callback 表，寫入時會走到 `dl_trigger_write()`

## `debugfs_create_u32()`

意思：

- 在 debugfs 裡快速建立一個可以讀寫或讀取 `u32` 變數的檔案

在 `01-debugfs-logging` 裡：

```c
debugfs_create_u32("trigger_count", 0444, dl_root, &dl_trigger_count);
```

你現在先記：

- 這種 helper 適合導出簡單 counter 或開關
- 不需要你自己寫完整 read callback

## `debugfs_remove()`

意思：

- 移除 debugfs entry

在這個 repo 裡：

- module exit 時會呼叫它，避免 module 卸載後 `/sys/kernel/debug/...` 留下失效入口

## `struct dentry`

意思：

- kernel VFS 裡代表 directory entry 的物件

對 `01-debugfs-logging` 的第一輪理解：

- `dl_root` 記住 debugfs 目錄
- 卸載時用它移除整棵 debugfs 目錄樹

你現在不需要深入 dcache 或 VFS 內部。

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

## `dev_t`

意思：

- kernel 用來表示 major/minor device number 的型別

你現在先記：

- major 大致代表哪一類 driver
- minor 大致代表同一個 driver 底下的哪一個 device
- `02-char-device` 會用 `alloc_chrdev_region()` 申請一個 `dev_t`

## major / minor

意思：

- char/block device node 背後的編號

第一輪理解：

- `/dev/driver_lab_char0` 看起來是路徑
- kernel 最後是靠 major/minor 找到對應的 char device 與 callback

## `alloc_chrdev_region()`

意思：

- 向 kernel 動態申請 char device 的 major/minor 編號範圍

在 `02-char-device` 裡：

- init path 先申請 device number
- exit path 要用 `unregister_chrdev_region()` 釋放

## `cdev`

意思：

- character device 的 kernel 物件

你現在先把它想成：

- 把某組 major/minor 與 `file_operations` callback 接起來的物件

## `cdev_init()` / `cdev_add()` / `cdev_del()`

意思：

- `cdev_init()`：初始化 char device 物件，指定 callback 表
- `cdev_add()`：把它正式加入 kernel
- `cdev_del()`：卸載時移除

第一輪先記：

- `cdev_add()` 成功後，VFS 才能透過這個 char device 找到 driver callback

## `class_create()`

意思：

- 建立 Linux device model 裡的一個 class

在教學第一輪，你可以先把它當成：

- 建立 `/dev/...` 節點前需要的一層分類資訊

不用急著理解完整 sysfs device model。

## `device_create()`

意思：

- 建立一個 device 物件，通常會讓系統建立對應 `/dev/...` 節點

在 `02-char-device` 裡：

- 它讓你最後看到 `/dev/driver_lab_char0`

## `device_destroy()` / `class_destroy()`

意思：

- `device_create()` / `class_create()` 的 cleanup 對應動作

你現在先記：

- init 取得的 resource，要在 exit 大致反向釋放

## `VFS`

意思：

- Virtual File System

你現在不用深究完整架構，只要知道：

- 它是 userspace 檔案操作進入 driver 的中間層

## `struct inode`

意思：

- VFS 用來代表檔案節點資訊的物件

在 `01-debugfs-logging` 裡：

- `dl_status_open(struct inode *inode, struct file *file)` 會收到它
- 第一輪只需要知道：open callback 會拿到 `inode`，然後交給 `single_open()`

## `struct file`

意思：

- VFS 用來代表「已開啟檔案」的物件

在 `01-debugfs-logging` 裡：

- `dl_status_open()` 和 `dl_trigger_write()` 都會收到 `struct file *`
- 第一輪只需要知道：read/write/open callback 會透過它知道目前操作的是哪個開啟檔案

## `file_operations`

意思：

- driver 提供給 VFS 的 callback 集合

常見成員：

- `.open`
- `.read`
- `.write`
- `.release`

在 `01-debugfs-logging` 裡：

- `dl_status_fops` 把 `cat status` 接到 `dl_status_open()` / `seq_read`
- `dl_trigger_fops` 把 `tee > trigger` 接到 `dl_trigger_write()`

在 `02-char-device` 裡：

- `dl_char_fops` 把 `/dev/driver_lab_char0` 的 `read/write` 接到 `dl_char_read()` / `dl_char_write()`

## `struct seq_file`

意思：

- kernel 用來產生可被 userspace 讀取的文字輸出 helper

你現在先把它想成：

- 比自己手刻一堆 read buffer 邏輯更安全、規則更清楚的輸出工具
- `seq_printf()` 寫進 `seq_file`，最後會變成 `cat status` 看到的文字

在 `01-debugfs-logging` 裡：

```c
static int dl_status_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "trigger_count=%u\n", dl_trigger_count);
}
```

第一輪只要知道：`dl_status_show()` 產生 `status` 的文字內容。

## `single_open()`

意思：

- `seq_file` 的簡化 open helper

在 `01-debugfs-logging` 裡：

- `cat status` 時，open path 會呼叫 `single_open()`
- `single_open()` 會把後續輸出接到 `dl_status_show()`

## `seq_read()` / `seq_lseek()` / `single_release()`

意思：

- `seq_file` 常見配套 callback

你現在先記：

- `.read = seq_read`：讓 userspace 可以讀 `seq_file` 產生的文字
- `.llseek = seq_lseek`：支援基本 seek 行為
- `.release = single_release`：關閉檔案時釋放 `single_open()` 建立的狀態

第一輪不用深入它們內部。

## `copy_from_user()`

意思：

- 從 userspace buffer 安全複製資料到 kernel buffer

為什麼需要：

- kernel 不能把 userspace 傳進來的 pointer 當成一般 kernel pointer 直接用
- 複製可能失敗，所以 driver 要檢查回傳值

在 `01-debugfs-logging` 裡：

- 寫 `trigger` 時，payload 會從 userspace 複製到 kernel stack 上的 local buffer

## `copy_to_user()`

意思：

- 從 kernel buffer 安全複製資料到 userspace buffer

為什麼需要：

- kernel 不能直接信任 userspace 傳進來的 pointer
- 複製可能失敗，所以 driver 要回報錯誤

在 `02-char-device` 裡：

- `read()` 會把 kernel buffer 的內容複製回 userspace

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

## `UAPI`

意思：

- User API，kernel 對 userspace 公開的 header / 常數 / struct 約定

在這個 repo 裡：

- `runtime/include/driver_lab_uapi.h` 定義 `03` 的 ioctl command 與 status struct

你現在先記：

- kernel driver 和 userspace runtime 要 include 同一份 UAPI，才不會各講各的格式

## `ioctl`

意思：

- 一條給 userspace 對 driver 下控制命令的 syscall path

你現在先把它想成：

- `read/write` 負責資料
- `ioctl` 負責命令或狀態查詢

在 `03-ioctl-poll-mmap` 裡：

- `DL_IOC_SET_MESSAGE`
- `DL_IOC_GET_STATUS`
- `DL_IOC_TRIGGER_EVENT`
- `DL_IOC_CLEAR_BUFFER`

## `_IOW` / `_IOR`

意思：

- Linux 常用來定義 ioctl command number 的 macro

第一輪先記：

- `_IOW` 大致表示 userspace 寫資料給 kernel
- `_IOR` 大致表示 kernel 回資料給 userspace
- 完整 bit layout 可以等之後再補

## waitqueue

意思：

- kernel 裡讓 task 等某個條件成立的等待佇列

在 `03` 裡：

- `poll()` 會把等待者接到 waitqueue
- driver 狀態改變時再喚醒它

## `poll`

意思：

- userspace 用來等待 fd 是否可讀、可寫或有事件的介面

你現在先記：

- 它避免 userspace 一直 busy loop 問「好了沒」
- `03` 用它等待 buffer 可讀或 event pending

## VMA

意思：

- Virtual Memory Area，process 虛擬位址空間裡的一段 mapping 描述

在 `03` 第一輪：

- 你只需要知道 `mmap()` callback 會拿到 VMA
- driver 會把受控的一頁 shared page 映射給 userspace

## `mmap`

意思：

- 把某段 kernel/driver 管理的 memory 映射到 userspace 位址空間

在 `03` 裡：

- 它映射的是 driver 維護的一頁 shared snapshot page
- 不是把任意 kernel memory 暴露出去

## non-blocking / `-EAGAIN`

意思：

- non-blocking fd 沒資料時不睡眠等待，而是立刻回錯誤

在 `03` 裡：

- buffer 為空且 fd 是 non-blocking 時，`read()` 可回 `-EAGAIN`

你現在先記：

- `-EAGAIN` 的意思通常是「現在還沒有，稍後再試」

## `mutex`

意思：

- mutual exclusion，用來保護同一時間只能有一條路徑進入某段 critical section

在 `04-locking-and-races` 裡：

- safe mode 用 `mutex` 保護共享 counter 的 increment

你現在先記：

- 多條路徑會改同一份 state 時，先問這段有沒有被 lock 保護

## race condition

意思：

- 結果取決於多條執行路徑的時間順序

在 `04` 裡：

- unsafe mode 故意讓多個 userspace thread 同時改 counter，觀察 lost update

## lost update

意思：

- 多條路徑同時做 read-modify-write，最後有些更新被覆蓋掉

白話：

- 兩個人都看到 counter 是 0，各自加一後都寫回 1，結果少算一次

## `kthread`

意思：

- kernel thread

在 `04` 裡：

- 背景 kthread 模擬 driver 內部也會同時碰共享 state

## `completion`

意思：

- kernel 裡等待「某件事完成」的同步工具

在 `06/07` 裡：

- probe 或 DMA path 會等待 IRQ handler 呼叫 `complete()`，確認事件真的發生

## PCI

意思：

- Peripheral Component Interconnect，在這個 repo 裡主要用來學 PCI/PCIe driver 的共通骨架

你現在先記：

- `05-07` 用 QEMU EDU device 模擬一顆 PCI device
- PCI core match vendor/device ID 後才會呼叫 driver `probe()`

## PCI ID

意思：

- PCI device 的 vendor ID 與 device ID

在 QEMU EDU 裡：

- ID 是 `1234:11e8`

如果 `lspci -nn | grep 1234:11e8` 找不到它，`05-07` 的 driver 不會進 `probe()`。

## `probe()` / `remove()`

意思：

- `probe()`：device match 後，kernel bus/core 交給 driver 接手時呼叫
- `remove()`：device 被移除或 driver 卸載時呼叫，用來 cleanup

在 `05-07` 裡：

- `probe()` 負責 enable PCI device、map BAR、申請 IRQ 或 DMA resource
- `remove()` 要反向釋放

## BAR

意思：

- Base Address Register，PCI device 暴露給 host 的位址窗口

在 EDU lab 裡：

- BAR0 是 MMIO register window

## MMIO

意思：

- Memory-Mapped I/O

你現在先記：

- 看起來像讀寫記憶體位址
- 實際上是在讀寫裝置 register
- 不是一般 RAM

## `ioread32()` / `iowrite32()`

意思：

- kernel driver 用來讀寫 32-bit MMIO register 的 helper

在 `05` 裡：

- 用它們讀 EDU identification / liveness register

## IRQ

意思：

- interrupt request，裝置通知 CPU/driver 有事件發生

在 `06` 裡：

- handler 會讀 status、寫 acknowledge、喚醒 completion

## MSI

意思：

- Message Signaled Interrupt，PCI 裝置用 message 形式送出的 interrupt

第一輪先記：

- 它和 legacy INTx 是不同 delivery 方式
- 本 repo 的 EDU IRQ lab 仍要求 handler 正確 acknowledge 裝置 status

## bus mastering

意思：

- 讓 PCI device 能主動發起 bus transaction 的能力

在 `06/07` 裡：

- MSI 與 DMA 都可能需要 device 主動對 host memory 做動作，所以 driver 會呼叫 `pci_set_master()`

## DMA

意思：

- Direct Memory Access，裝置直接搬資料到記憶體或從記憶體搬資料

你現在先記：

- CPU 仍負責設定 register 與驗證結果
- 真正 payload 搬運由 device 做

## coherent DMA buffer

意思：

- CPU 和 device 都能安全存取的一塊 DMA buffer

在 `07` 裡：

- CPU 用 kernel pointer 存取它
- device 用 DMA address 存取它

## `dma_addr_t`

意思：

- kernel 用來表示 device 看到的 DMA address 的型別

第一輪先記：

- 它不是一般 C pointer
- 不要拿它直接解參考

## DMA mask

意思：

- 裝置可定址的 DMA address 範圍限制

在 EDU lab：

- 使用 28-bit DMA mask，符合 QEMU EDU 的教學限制

## runtime

意思：

- userspace library，包裝 driver ABI，讓 CLI/app 不用到處直接散寫 syscall

在這個 repo 裡：

- `runtime/` 目前主要包 `02/03` 用到的 `open/read/write/ioctl/poll/mmap`

第一輪先記：

- runtime 不是 kernel driver
- 它不會產生 `.ko`

## API vs ABI

意思：

- API 是程式碼呼叫介面
- ABI 是 binary/interface 約定，userspace 和 kernel 都要照同一份格式互動

在這個 repo 裡：

- UAPI header 是 ABI 約定
- runtime header 是 userspace API

## smoke test

意思：

- 最小成功路徑測試

你現在先記：

- smoke test 通過代表基本路徑可跑
- 不代表所有 race、error path、長時間壓力都驗過

## stress test

意思：

- 重複或並行施壓，讓偶發問題更容易出現

在 `09` 裡：

- repeated load/unload 驗 cleanup 對稱性
- parallel access 驗共享狀態壓力

## regression

意思：

- 每次修改後固定重跑的檢查，避免舊功能被改壞

第一輪先記：

- regression 是紀律，不一定是一個特定工具

## fault injection

意思：

- 主動讓錯誤路徑發生，確認 driver cleanup 與 error handling 正確

例子：

- allocation 失敗
- page allocation 失敗
- usercopy 失敗

`09` 目前還沒有把這些做成完整自動化。

## KUnit

意思：

- Linux kernel 的 unit testing framework

第一輪先記：

- 適合測 kernel 內可拆出來的 helper logic
- 目前 repo 還沒把 driver logic 拆成需要 KUnit 的形狀

## kselftest

意思：

- Linux kernel tree 裡的 selftest framework，常用 userspace 測已 boot kernel 的對外行為

第一輪先記：

- 它比較像 regression/integration 測試入口
- 不是本 repo 目前 `09` 已完成的內容
