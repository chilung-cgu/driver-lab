# 05 到 07：PCI、IRQ、DMA 過渡導讀

`05-07` 是同一條硬體互動路線，不是三個互不相關的 lab。

你可以先這樣記：

```text
05：我能碰到 device register
06：device 能通知我事件
07：device 能自己搬資料，driver 能驗證結果
```

## `05` 是 MMIO 起點

`05-pci-edu-mmio` 的核心問題只有三個：

1. guest 內有沒有 QEMU EDU device？
2. PCI core 有沒有呼叫 driver `probe()`？
3. BAR0 map 後能不能讀寫 EDU register？

所以第一個查證點永遠是：

```sh
lspci -nn | grep 1234:11e8
```

如果 guest 看不到 `1234:11e8`，`probe()` 不會進來。這不是 driver bug，而是測試環境還沒有 EDU device。

## `06` 為什麼接在 `05` 後面？

`05` 是 CPU 主動問裝置。

`06` 是裝置主動通知 CPU。

在 EDU 裝置裡，IRQ path 需要理解三個 register 概念：

| register | 第一輪理解 |
|---|---|
| interrupt raise `0x60` | driver 寫它，要求 EDU 產生一次 interrupt |
| interrupt status `0x24` | handler 讀它，確認是哪個 interrupt source |
| interrupt acknowledge `0x64` | handler 寫它，清掉裝置端 pending 狀態 |

IRQ handler 不能只印 log。它至少要：

1. 讀 status。
2. 判斷是不是自己的事件。
3. 寫 acknowledge。
4. 喚醒等待 completion 的路徑。

## MSI 與 `pci_set_master()` 的事故筆記

這個 repo 曾經踩過一個重要問題：

> 若 PCI core 配到 MSI，EDU 要能送出 MSI，就需要 bus mastering。少了 `pci_set_master()` 時，interrupt raise 可能只設了 status bit，但 handler 沒被叫到。

第一輪不用深入 MSI message 的格式。先記住：

- INTx/MSI 是不同 interrupt delivery 方式。
- handler 邏輯仍然要 acknowledge EDU interrupt status。
- bus mastering 是讓 device 能主動發起某些 bus transaction 的必要能力之一。

## `07` 為什麼接在 `06` 後面？

DMA 完成後通常需要通知 driver。這個通知常常走 IRQ。

所以 `07` 不只是「多一個 DMA API」。它是把前面兩件事接起來：

- `05`：driver 能寫 DMA register。
- `06`：driver 能等 DMA completion interrupt。
- `07`：driver 配 DMA buffer，讓 EDU 搬資料，IRQ 到了再驗結果。

## coherent DMA buffer 的兩種視角

`dma_alloc_coherent()` 會給 driver 兩個重要值：

| 視角 | 用途 |
|---|---|
| CPU pointer | kernel code 用來填資料、讀結果 |
| DMA address | device 用來知道要搬哪裡的資料 |

它們指向同一塊可共享的 buffer，但不是同一種位址語意。

第一輪最重要的是不要把 DMA address 當成一般 C pointer。

## `07` 的 round-trip 在驗什麼？

round-trip 是：

1. CPU 在 tx buffer 填測試資料。
2. driver 設定 EDU DMA register，做 RAM -> EDU。
3. driver 再設定 EDU DMA register，做 EDU -> RAM。
4. CPU 用 `memcmp()` 比對 tx/rx buffer。

如果 `memcmp()` 通過，代表至少這條最小 DMA path 可以把資料來回搬對。

## 進 `06/07` 前你要能回答

| 問題 | 標準答案 |
|---|---|
| `05` 裡 BAR0 是什麼？ | BAR0 是 QEMU EDU 的 MMIO register window；driver 用 `pci_iomap()` map 後，再用 `ioread32()` / `iowrite32()` 讀寫 register。 |
| `probe()` 沒進來時第一個看哪裡？ | 先在 guest 內跑 `lspci -nn | grep 1234:11e8`，確認 QEMU EDU device 真的存在。 |
| IRQ handler 為什麼要 acknowledge？ | handler 讀 status 後要寫 acknowledge register 清掉裝置端 pending 狀態，否則同一個 interrupt 可能一直重進。 |
| completion 在 `06/07` 裡等待的是什麼？ | 等 IRQ handler 確認事件發生並呼叫 `complete()`；`06` 等自我測試 interrupt，`07` 等 DMA transfer completion interrupt。 |
| coherent DMA buffer 的 CPU pointer 和 DMA address 差在哪？ | CPU pointer 給 kernel code 讀寫 buffer；DMA address 給 device 寫進 register 用來搬資料。DMA address 不是一般 C pointer，不能直接解參考。 |

## 第一輪可以先略過

- INTx/MSI/MSI-X 完整硬體差異。
- interrupt affinity。
- streaming DMA API。
- IOMMU 與 scatter-gather。
- 真實硬體 reset、AER、power management。

先把 EDU 的 MMIO、IRQ acknowledge、coherent DMA round-trip 串起來。
