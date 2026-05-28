# 07 - PCI EDU DMA

## 目標

在 `edu` 裝置上補上 DMA buffer 與 round-trip 驗證。

> [!NOTE]
> 這一關現在已經有第一版可 build 的 driver code 與 smoke test。
> 真正的載入與驗證仍必須在 Linux guest 內完成。

## 開始前先看

- [`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md)
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

## 這一關會出現哪些 filesystem 入口

`07` 仍是 PCI driver，不會把 coherent DMA buffer 變成一個 `/dev` 或 `/sys` 裡的普通檔案。

| 入口 | 第一輪用途 |
|---|---|
| `lspci -nn | grep 1234:11e8` | 確認 EDU device 存在。 |
| `/sys/bus/pci/devices/...` | 觀察 PCI device / driver bind 狀態。 |
| `/sys/bus/pci/drivers/driver_lab_edu_dma` | 觀察 DMA lab 的 PCI driver 是否註冊。 |
| `/proc/interrupts` | 輔助觀察 DMA completion IRQ。 |
| `dmesg` | 主要驗收：`dma mask configured`、`coherent buffer allocated`、`round-trip compare passed`。 |

第一輪請記住：DMA buffer 有 CPU pointer 和 device DMA address 兩種視角，但它不是給你 `cat /sys/...` 讀的普通檔案。

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

`test.sh` 逐段在驗什麼：

1. 確認目前是 Linux，並確認 guest 看得到 `1234:11e8`。
2. `make` 建出 `driver_lab_edu_dma.ko`。
3. 如果前一次留下同名 module，先卸載，避免 DMA/IRQ resource 狀態混亂。
4. 清本次 `dmesg` 後載入 module。
5. 檢查 PCI driver sysfs directory、bind 狀態，以及 `/proc/interrupts` 是否列出 `driver_lab_edu_dma`。
6. grep `dma mask configured`，確認 device 可定址範圍已設定。
7. grep `coherent buffer allocated`，確認 CPU/device 共享 buffer 配置成功。
8. grep `round-trip compare passed`，確認 RAM -> EDU -> RAM 的資料一致。
9. 卸載 module，確認 PCI driver sysfs directory 消失，並 `make clean`。

第一輪卡住時，先把問題拆成三段：DMA mask、buffer allocation、round-trip compare，不要一次追完整 DMA subsystem。

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | coherent DMA buffer 同時有 CPU pointer 和 device DMA address；round-trip 是 RAM -> EDU -> RAM；最後用 `memcmp()` 驗資料一致。 |
| 可以先略過 | streaming DMA API、cache coherency 細節、IOMMU、scatter-gather、DMA engine framework。 |
| 之後再回來補 | DMA direction、mapping lifetime、硬體 DMA mask 限制、錯誤路徑如何避免 buffer 或 IRQ resource 泄漏。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| coherent DMA buffer 有哪兩種視角？ | CPU 用 kernel pointer 存取同一塊 buffer；裝置用 DMA address 存取同一塊 buffer。 |
| 為什麼要先設 DMA mask？ | 要確認裝置能定址 driver 分配給它的 DMA address 範圍；EDU lab 使用 28-bit mask。 |
| round-trip 驗證在驗什麼？ | 先 RAM -> EDU，再 EDU -> RAM，最後用 `memcmp()` 比對 tx/rx buffer 是否一致。 |
| 第一個觀測點是什麼？ | `dmesg` 中的 `dma mask configured`、`coherent buffer allocated`、`round-trip compare passed`。 |
| 這一關主要拿到什麼 resource？ | BAR0 MMIO mapping、IRQ vector/handler、coherent DMA buffer。 |
| cleanup 要釋放哪些東西？ | `free_irq()`、`pci_free_irq_vectors()`、`dma_free_coherent()`、`pci_iounmap()`、`pci_release_region()`、`pci_disable_device()`。 |
| round-trip 失敗時第一個看哪裡？ | 先核對 DMA source/destination register、方向 bit、count，再看 timeout 或 IRQ completion log。 |

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

遇到 kernel API 時，先套用「參數角色」模板，完整方法見 [`../../docs/onboarding/kernel-api-parameter-roles.md`](../../docs/onboarding/kernel-api-parameter-roles.md)。

| API | 參數角色 | 第一輪理解 |
|---|---|---|
| `dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28))` | device、可定址範圍 | 告訴 DMA API 這顆 EDU 可用的 DMA address 範圍。 |
| `dma_alloc_coherent(&pdev->dev, size, &dl->dma_handle, GFP_KERNEL)` | device、size、DMA address output、allocation flag | 回傳 CPU pointer，並把 device 要用的 DMA address 填進 `dma_handle`。 |
| `dl_edu_dma_program_addrs(dl, src, dst)` | per-device state、source DMA address、destination DMA address | 寫進 EDU DMA source/destination register；這些是 device 視角的位址。 |
| `dl_edu_dma_run_once(dl, src, dst, cmd, phase)` | device state、位址、命令、log 名稱 | 設定一次 DMA transfer，等 IRQ 與 command bit 完成。 |
| `dma_free_coherent(&pdev->dev, size, dl->dma_buf, dl->dma_handle)` | device、size、CPU pointer、DMA address | 釋放前面 coherent allocation 拿到的兩種位址。 |

先不要把 DMA 想成 `memcpy()`。這裡的核心是「裝置拿到一個它能用的位址，自己去搬資料」。

## 第一次卡住先看哪裡

- DMA mask 設不過
  - 先回去看 QEMU EDU 預設 `dma_mask` 限制
- 資料方向不對
  - 先重新核對 source / destination register
- round-trip 結果不對
  - 先把傳輸長度壓小，再逐步確認每一步
