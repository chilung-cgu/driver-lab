# QEMU EDU 新手起手式

這份文件是給第一次碰 PCIe driver 的人看的。

目標不是一次把 `05`、`06`、`07` 全做完，而是先建立一條不容易迷路的順序。

## 先有一個正確預期

QEMU `edu` 裝置不是「假裝很完整的 AI 加速卡」。

它只是官方提供的教學裝置，用來練：

- PCI 裝置被 kernel 掃到
- driver `probe()`
- BAR / MMIO
- interrupt
- DMA

官方文件明講它是給寫 kernel driver 教學使用的，並提供固定 PCI ID、MMIO、IRQ、DMA 規格。

## 05、06、07 三關不是三個完全不同的世界

你可以把它們想成同一支 driver 的三個擴充階段：

1. `05-pci-edu-mmio`
   - 我有沒有成功接手這顆 PCI device？
   - 我能不能 map BAR 並讀第一個 register？
2. `06-pci-edu-irq`
   - 裝置發中斷時，我能不能接住並正確清掉？
3. `07-pci-edu-dma`
   - 我能不能讓裝置直接存取一塊可見的記憶體，並驗證搬資料結果？

## 第一次只要記住這些關鍵名詞

### PCI ID

QEMU EDU 的官方 PCI ID 是：

```text
1234:11e8
```

這是你讓 driver `id_table` 去 match 的起點。

### BAR0

EDU 的 Region 0 是一塊 `1 MB` 的 MMIO 空間。

你未來做的很多事情都會經過這塊空間：

- 讀 identification register
- 做 liveness check
- 觸發 interrupt
- 設 DMA source / destination / count / command

### MMIO

這裡不要把它想成一般 RAM 指標。

你是在透過 BAR map 出來的 register window 跟裝置對話。

## 你第一次最該記住的 register

以下內容依 QEMU 官方 `EDU device` 規格整理：

| Offset | 用途 | 第一次拿來做什麼 |
| --- | --- | --- |
| `0x00` | identification | 確認你真的讀到裝置 |
| `0x04` | liveness check | 做最小 read/write 對照 |
| `0x20` | status | 之後看 factorial / DMA 狀態 |
| `0x24` | interrupt status | 確認哪個事件引發中斷 |
| `0x60` | interrupt raise | 人工觸發中斷 |
| `0x64` | interrupt acknowledge | 在 handler 裡清中斷 |
| `0x80` | DMA source | 設 DMA 來源位址 |
| `0x88` | DMA destination | 設 DMA 目的位址 |
| `0x90` | DMA transfer count | 設傳輸長度 |
| `0x98` | DMA command | 啟動 DMA |

## 三關的第一次驗收畫面

### `05-pci-edu-mmio`

你第一次理想上要看到：

- guest 裡 `lspci -nn` 真的看到 `1234:11e8`
- module 載入時 `probe()` 有 log
- log 裡能印出 BAR0 已 map
- 能讀到 identification register

### `06-pci-edu-irq`

你第一次理想上要看到：

- `request_irq()` 成功
- 你寫 interrupt raise register 後，handler 有進來
- handler 有把 acknowledge register 寫回去
- 中斷不會卡住反覆重進

### `07-pci-edu-dma`

你第一次理想上要看到：

- `dma_set_mask_and_coherent()` 成功
- DMA buffer 配置成功
- 一次 RAM -> EDU、再 EDU -> RAM 的 round-trip 可驗證
- timeout 或失敗時能清掉資源

## 第一次最容易卡住的地方

### guest 裡根本看不到 `1234:11e8`

先不要看 driver code。

先檢查：

- QEMU 啟動參數裡有沒有 `-device edu`
- guest 裡 `lspci` 是否可用

### driver 沒有進 `probe()`

先檢查：

- PCI ID table 是否真的有 `1234:11e8`
- module 是否真的載入成功
- 這顆裝置有沒有被別的 driver 抓走

### BAR map 失敗

先檢查：

- `pci_enable_device()` 是否成功
- resource flags 是否合理
- 你是不是在錯的 BAR index 上做 `pci_iomap()`

### 中斷一直停不下來

這通常表示：

- handler 沒有把 interrupt acknowledge register 清掉

QEMU 官方文件明確說，即使用 MSI，也還是要更新 acknowledge register。

### DMA 設定好像對，但資料不對

先不要急著懷疑 CPU copy。

先檢查：

- DMA mask 是否設對
- 來源 / 目的位址方向是否寫反
- count 是否超出 EDU 提供的 buffer 範圍

## 正確的學習順序

第一次只做下面這條：

1. 讀 [`../docs/pcie-primer.md`](pcie-primer.md)
2. 跑 [`../qemu/launch-edu-vm.sh`](../qemu/launch-edu-vm.sh)
3. 在 guest 確認 `lspci -nn`
4. 先完成 `05`
5. `05` 穩後才做 `06`
6. `06` 穩後才做 `07`

不要一開始就把 MMIO、IRQ、DMA 混在同一支第一版 driver 裡。

## 最後要建立的心智模型

這三關本質上在練：

- `05`：裝置發現與基本對話
- `06`：事件通知與清除
- `07`：裝置與主記憶體之間的資料搬運

這就是未來 AI 加速卡 host driver 的最小骨架。
