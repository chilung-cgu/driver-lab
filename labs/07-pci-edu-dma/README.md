# 07 - PCI EDU DMA

## 目標

在 `edu` 裝置上補上 DMA buffer 與 round-trip 驗證。

## 開始前先看

- [`../../docs/pcie-primer.md`](../../docs/pcie-primer.md)

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

## 第一次只要先懂這件事

這一關不是在學「CPU 自己 copy 一段記憶體」。

而是在學：

- 哪一塊 buffer 可以安全地暴露給裝置
- 裝置搬完資料後，你怎麼確認它真的對

## 第一次實作順序

1. 先設 DMA mask
2. 再分配 coherent buffer
3. 再填測試資料
4. 再寫 DMA register
5. 最後驗 round-trip 結果

## 第一次驗收時你要看到什麼

- buffer 配置成功
- DMA 完成後資料一致
- timeout / error path 能清乾淨

## 新手先記住這一關在補什麼

- 這一關不只是「搬資料」
- 你在學的是：哪些記憶體可以安全地讓裝置看到，以及失敗時怎麼收乾淨
