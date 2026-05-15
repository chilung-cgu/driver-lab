# 07 - PCI EDU DMA

## 目標

在 `edu` 裝置上補上 DMA buffer 與 round-trip 驗證。

> [!NOTE]
> 這一關現在已經有第一版可 build 的 driver code 與 smoke test。
> 真正的載入與驗證仍必須在 Linux guest 內完成。

## 開始前先看

- [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- [`../../docs/guides/qemu-edu-first-pass.md`](../../docs/guides/qemu-edu-first-pass.md)

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

## 目前已實作的內容

- `dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28))`
- `dma_alloc_coherent()`
- RAM -> EDU 的 DMA
- EDU -> RAM 的 DMA
- interrupt completion path
- 最後用 `memcmp()` 驗 round-trip 結果

主要檔案：

- [`driver_lab_edu_dma.c`](driver_lab_edu_dma.c)
- [`test.sh`](test.sh)

## 第一次驗收時你要看到什麼

- buffer 配置成功
- DMA 完成後資料一致
- timeout / error path 能清乾淨

## 第一次理想上要看到的輸出

`dmesg` 裡第一版通常至少要看到：

```text
driver_lab_edu: dma mask configured
driver_lab_edu: coherent buffer allocated
driver_lab_edu: dma transfer finished
driver_lab_edu: round-trip compare passed
```

如果第一次做不到這麼完整，也沒關係。

你可以先把驗收拆成：

1. DMA mask 先成功
2. buffer 先配置成功
3. 再追 round-trip

## 現在怎麼跑

```sh
cd labs/07-pci-edu-dma
./test.sh
```

這支腳本會 build module，載入後檢查：

- `dma mask configured`
- `coherent buffer allocated`
- `round-trip compare passed`

## 新手先記住這一關在補什麼

- 這一關不只是「搬資料」
- 你在學的是：哪些記憶體可以安全地讓裝置看到，以及失敗時怎麼收乾淨

## 看 source code 時先抓哪幾個點

DMA 這關容易一次看到太多名詞。第一次先抓「buffer 是誰看得到」：

1. `dl_edu_dma_probe()`：先完成 PCI enable、BAR map、IRQ setup，再進 DMA setup
2. `dma_set_mask_and_coherent()`：先確認裝置能定址 driver 要給它的 DMA 位址範圍
3. `dma_alloc_coherent()`：配置 CPU 與 device 都能安全存取的 coherent buffer
4. `dl_edu_dma_program_addrs()`：把 source / destination DMA address 寫進 EDU register
5. `dl_edu_dma_run_once()`：每次 DMA transfer 如何設定 count、command、等待完成
6. `dl_edu_dma_handler()`：DMA 完成中斷如何 acknowledge 並喚醒等待路徑
7. `dl_edu_dma_remove()`：卸載時如何 free IRQ、釋放 coherent buffer、unmap BAR

先不要把 DMA 想成 `memcpy()`。這裡的核心是「裝置拿到一個它能用的位址，自己去搬資料」。

## 第一次卡住先看哪裡

- DMA mask 設不過
  - 先回去看 QEMU EDU 預設 `dma_mask` 限制
- 資料方向不對
  - 先重新核對 source / destination register
- round-trip 結果不對
  - 先把傳輸長度壓小，再逐步確認每一步
