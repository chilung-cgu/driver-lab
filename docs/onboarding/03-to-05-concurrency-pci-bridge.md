# 03 到 05：併發與 PCI 過渡導讀

`03` 做完後，你已經不只是會寫一個能被 userspace 呼叫的 driver。你已經開始有多條 ABI path 共同碰同一份 kernel state。

這就是為什麼下一步先進 `04-locking-and-races`，再進 `05-pci-edu-mmio`。

## 先回答：為什麼 `03` 後不是直接跳 PCI？

因為 PCI driver 也逃不掉同樣問題：

- `probe()` 取得 resource
- userspace 或內部 worker 可能碰 state
- IRQ path 可能同時進來
- remove/unload 時要停掉所有還可能碰 resource 的路徑

如果你在沒有硬體的 `04` 先練過 race 與 cleanup，進 `05-07` 時會比較知道自己在保護什麼。

## `03 -> 04`：多條 ABI path 代表多條執行路徑

`03` 已經有：

- `read/write`
- `ioctl`
- `poll`
- `mmap`

這些路徑可能被不同 process 或 thread 同時呼叫。只要它們碰同一份 state，就要問：

1. 哪些 state 是共享的？
2. 哪些 callback 會讀它？
3. 哪些 callback 會改它？
4. 改它時有沒有 lock？
5. cleanup 時有沒有人還可能正在用？

`04` 的 unsafe mode 是刻意讓你看到這件事真的會壞，不是理論問題。

## lost update 第一輪怎麼理解？

把 `counter++` 拆開來看，其實是三步：

1. 讀 counter
2. 加一
3. 寫回 counter

如果兩條 path 同時做這三步，就可能都讀到舊值，最後少算一次。這就是 lost update。

`04` 的 safe mode 用 mutex 包住這段 read-modify-write，讓同一時間只有一條 path 可以改 counter。

## `04 -> 05`：從自己建立入口，到 PCI core 指派裝置

`00-04` 的入口大多是 module init 自己建立的：

- debugfs entry
- `/dev/driver_lab_char0`
- `/dev/driver_lab_ctl0`
- `/dev/driver_lab_race0`

`05` 開始，入口換成 PCI core：

1. QEMU guest 開機時有一顆 EDU device。
2. kernel PCI core 掃到 `1234:11e8`。
3. driver 的 PCI ID table match。
4. PCI core 呼叫 `probe()`。
5. driver 在 `probe()` 裡 enable device、request BAR、map BAR。

你不用自己從 shell 呼叫 `probe()`。你只能提供 callback，等 PCI core 在 match 後呼叫。

## 進 `05` 前要懂的 resource 差異

| 前面 labs | `05-pci-edu-mmio` |
|---|---|
| resource 多半是 debugfs、char device、page、kthread | resource 是 PCI device enable 狀態、BAR region、MMIO mapping |
| cleanup 多半在 module exit | cleanup 可能在 PCI `remove()` |
| 觀測點多半是 `/dev`、debugfs、CLI | 觀測點先是 `lspci` 與 `dmesg` |
| 沒有真硬體也能跑 `00-04` | `05` 需要 guest 內看得到 QEMU EDU |

## BAR 與 MMIO 第一輪怎麼理解？

BAR 是 PCI device 暴露給 host 的 address window。

在 EDU lab，BAR0 是 MMIO register window。driver 透過：

- `pci_request_region()`：宣告這段 BAR 由我使用。
- `pci_iomap()`：把 BAR 變成 driver 可以用的 MMIO pointer。
- `ioread32()` / `iowrite32()`：讀寫 register。

第一輪不要把 MMIO 當成普通 RAM。讀寫 MMIO 是在和裝置對話。

## 進 `05` 前你要能回答

1. `04` 裡哪些 state 被多條路徑共享？
2. unsafe mode 為什麼會 lost update？
3. mutex 保護的是哪一段 read-modify-write？
4. `probe()` 是誰呼叫的？
5. `lspci -nn | grep 1234:11e8` 沒看到東西時，為什麼不能怪 driver code？

## 第一輪可以先略過

- PCI enumeration 完整流程。
- BAR assignment 的平台細節。
- `spinlock`、atomic、lockdep 的完整使用策略。
- AER、reset、power management。

先把「共享狀態要保護」與「PCI device match 後才進 `probe()`」講清楚。
