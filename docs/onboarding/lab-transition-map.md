# Lab 過渡地圖

這份文件只回答一件事：

> 每一關為什麼會接到下一關？我進下一關前要先補哪個心智模型？

它不是取代各 lab README，而是讓你在章節之間不要突然撞到一堆陌生名詞。

## 先看整體分段

| 階段 | Labs | 主題 | 新手第一輪目標 |
|---|---|---|---|
| 基礎閉環 | `00-02` | module lifecycle、debugfs、char device | 知道 userspace 操作如何進 driver callback |
| ABI 與共享狀態 | `03-04` | `ioctl` / `poll` / `mmap`、race、mutex | 知道多條操作路徑會碰同一份 kernel state |
| PCI/QEMU EDU | `05-07` | PCI probe、MMIO、IRQ、DMA | 知道 driver 如何接手一個 PCI device 並和裝置互動 |
| runtime 與驗證 | `08-09` | userspace runtime、stress、fault injection | 知道怎麼把 driver 使用方式包起來，並反覆驗證 |

## `01 -> 02`：從 debugfs 到 `/dev`

`01` 的 debugfs 是 debug 觀測入口。它讓你用 `/sys/kernel/debug/...` 讀寫 driver 暴露的狀態。

`02` 的 `/dev/driver_lab_char0` 是 char device 入口。它更接近正式 driver 會提供給 userspace 的資料路徑。

第一輪只要抓住：

- 兩者都會經過 VFS 與 `file_operations` callback。
- debugfs 是 debug 介面，不是穩定產品 ABI。
- `/dev/driver_lab_char0` 是 userspace 對 driver 做 `read()` / `write()` 的主要入口。

進 `02` 前，先讀 [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md) 的前半段。

## `02 -> 03`：從 `read/write` 到多條 ABI 路徑

`02` 只有最小 data path：寫入一段資料，再讀回來。

`03` 會把同一個 device node 擴成四條路：

- data path：`read()` / `write()`
- control path：`ioctl`
- event path：`poll`
- shared memory path：`mmap`

第一輪不要追完整 VMA、`poll_table` 或 `_IOW/_IOR` bit layout。你只需要能把 CLI subcommand 對到 driver callback。

進 `03` 前，先讀 [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md) 的後半段。

## `03 -> 04`：從 ABI 到 race

`03` 讓同一個 driver 有多條入口。只要多條入口共享同一份 state，就會開始有同步問題。

`04` 故意把 race 做出來，再用 mutex 修掉第一層問題。

第一輪只要抓住：

- race 不是「多執行緒程式才有」，driver callback 也可能被多個 process 同時呼叫。
- lost update 的核心是 read-modify-write 沒有被保護。
- cleanup 也有 lifetime 問題：背景 thread 或等待者不能碰已釋放的資源。

進 `04` 前，讀 [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md) 的併發段落。

## `04 -> 05`：從軟體 device node 到 PCI device

`00-04` 的 device node 都是 module 自己建立的教學入口。

`05` 開始，driver 要接手一個 QEMU 提供的 PCI device。入口不再只是 `insmod` 後自己建立 `/dev`，而是 PCI core 掃到 `1234:11e8` 並 match ID 後呼叫 `probe()`。

第一輪只要抓住：

- `probe()` 是 PCI core 在 match 後呼叫，不是你自己從 shell 直接呼叫。
- BAR0 是 MMIO register window。
- `ioread32()` / `iowrite32()` 是 CPU 透過 MMIO 讀寫裝置 register。

進 `05` 前，讀 [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md) 的 PCI 段落。

## `05 -> 06 -> 07`：從 MMIO 到 IRQ，再到 DMA

`05` 是 CPU 主動讀寫 register。

`06` 是裝置主動通知 driver：「事件發生了，請進 IRQ handler」。

`07` 是裝置不只通知你，還會自己搬資料；driver 要提供裝置可定址的 DMA buffer。

第一輪只要抓住：

- `05` 驗證你能碰到裝置 register。
- `06` 驗證 handler 會讀 status、寫 acknowledge、喚醒 completion。
- `07` 驗證 coherent DMA buffer 同時有 CPU pointer 與 device DMA address。

進 `06/07` 前，讀 [`05-to-07-pci-irq-dma-bridge.md`](05-to-07-pci-irq-dma-bridge.md)。

## `07 -> 08 -> 09`：從 driver 到使用與驗證習慣

`08` 不是新的 kernel driver。它是 userspace runtime，把前面 labs 的 ABI 包成比較一致的 C API。

`09` 也不是新硬體功能。它是驗證習慣：重複載入卸載、parallel access、之後再擴充 fault injection / KUnit / kselftest。

第一輪只要抓住：

- runtime 是 userspace library，不是 `.ko`。
- stress 是重複施壓。
- regression 是每次修改後固定重跑。
- fault injection 是主動讓錯誤路徑發生。

進 `08/09` 前，讀 [`07-to-09-runtime-validation-bridge.md`](07-to-09-runtime-validation-bridge.md)。
