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
