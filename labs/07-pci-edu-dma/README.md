# 07 - PCI EDU DMA

## 目標

在 `edu` 裝置上補上 DMA buffer 與 round-trip 驗證。

## 先備條件

- `05` 與 `06` 已完成
- 你已經理解 userspace buffer、kernel buffer、device-visible buffer 不是同一件事

## 這一關要練什麼

- `dma_set_mask_and_coherent()`
- coherent DMA buffer
- DMA register programming
- completion / poll / timeout

## 成功標準

- DMA mask 設定正確
- buffer 分配成功
- round-trip data 驗證成功
- 錯誤與 timeout path 有 cleanup

## 新手先記住這一關在補什麼

- 這一關不只是「搬資料」
- 你在學的是：哪些記憶體可以安全地讓裝置看到，以及失敗時怎麼收乾淨
