# 06 - PCI EDU IRQ

## 目標

在 `edu` 裝置上補上 interrupt path。

> [!NOTE]
> 這一關現在已經有第一版可 build 的 driver code 與 smoke test。
> 真正的載入與驗證仍必須在 Linux guest 內完成。

## 開始前先看

- [`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md)
- [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- [`../../docs/guides/qemu-edu-first-pass.md`](../../docs/guides/qemu-edu-first-pass.md)

## 先備條件

- `05-pci-edu-mmio` 已完成
- 你知道 `probe` 成功後 driver 目前已拿到哪些 resource

## 這一關要練什麼

- request IRQ
- INTx / MSI 基本概念
- interrupt status / acknowledge
- top-half 最小設計

## 成功標準

- 能成功註冊 IRQ handler
- 能觸發中斷
- handler 會清 interrupt acknowledge register

## 第一次只要先懂這件事

前一關你是主動去讀 register。

這一關開始，是裝置主動通知你：

> 有事情了，請進 handler 看一下。

## 第一次實作順序

1. 先保證 `05` 的 `probe()` 已穩定
2. 再註冊 IRQ handler
3. 再找方法觸發中斷
4. 最後確認 handler 真的清掉 acknowledge register

## 目前已實作的內容

- `pci_alloc_irq_vectors()`
- `request_irq()` / `free_irq()`
- 用 `0x60` interrupt raise register 觸發中斷
- handler 讀 `0x24` interrupt status
- handler 寫 `0x64` acknowledge register
- completion-based 的最小自我測試

主要檔案：

- [`driver_lab_edu_irq.c`](driver_lab_edu_irq.c)
- [`test.sh`](test.sh)

## 第一次驗收時你要看到什麼

- `request_irq()` 成功
- handler 有 log
- 同一個中斷不會無限重進

## 第一次理想上要看到的輸出

`dmesg` 裡第一版通常至少要看到：

```text
driver_lab_edu_irq: request_irq ok
driver_lab_edu_irq: irq status=0x00000001 acknowledged
driver_lab_edu_irq: irq self-test passed
```

這裡最重要的不是 log 漂不漂亮，而是：

- handler 真的進來
- acknowledge 真的做了

## 現在怎麼跑

```sh
cd labs/06-pci-edu-irq
./test.sh
```

這支腳本會 build module，載入後檢查：

- `request_irq ok`
- `irq status=...`
- `irq self-test passed`

`test.sh` 逐段在驗什麼：

1. 確認目前是 Linux，並確認 guest 看得到 `1234:11e8`。
2. `make` 建出 `driver_lab_edu_irq.ko`。
3. 如果前一次留下同名 module，先卸載，避免 IRQ/vector 狀態混亂。
4. 清本次 `dmesg` 後載入 module。
5. grep `request_irq ok`，確認 IRQ handler registration 成功。
6. grep `irq status=`，確認 handler 真的進來並讀到 status。
7. grep `irq self-test passed`，確認 completion 等到 handler 結果。
8. 卸載 module 並 `make clean`。

第一輪不要只看「有沒有 log」。真正重點是 handler 有沒有 acknowledge，否則中斷可能一直重進。

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | IRQ 是裝置通知 driver 的路徑；handler 要讀 status、判斷事件、寫 acknowledge、喚醒 completion；MSI 需要 bus mastering。 |
| 可以先略過 | INTx/MSI/MSI-X 的完整硬體差異；interrupt affinity；threaded IRQ；shared IRQ 的所有 corner cases。 |
| 之後再回來補 | 為什麼 handler 要短、哪些工作不能在 hard IRQ context 做、真實裝置如何設計多個 IRQ vector。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這一關建立在哪一關的基礎上？ | 建立在 `05` 的 PCI enable、BAR map、MMIO register access 之上，再加入 IRQ path。 |
| IRQ handler 要做什麼？ | 不只印 log；它要讀 interrupt status、判斷是不是自己的事件、寫 acknowledge register，最後喚醒 completion。 |
| 這一關如何觸發中斷？ | probe 自我測試會寫 EDU 的 interrupt raise register `0x60`。 |
| 為什麼 `06` 也呼叫 `pci_set_master()`？ | 因為 PCI MSI 是 device-originated memory write；若 PCI core 配到 MSI，裝置需要 bus mastering 才能把 MSI 送出。 |
| 第一個觀測點是什麼？ | `dmesg` 中的 `request_irq ok`、`irq status=`、`irq self-test passed`。 |
| 這一關主要拿到什麼 resource？ | BAR0 MMIO mapping、IRQ vector、IRQ handler registration。 |
| cleanup 要釋放哪些東西？ | 先 `free_irq()` 與 `pci_free_irq_vectors()`，再 unmap BAR、release region、disable device。 |
| handler 一直重進時第一個看哪裡？ | 優先檢查 handler 是否正確寫 interrupt acknowledge register `0x64`。 |

## 新手先記住這一關在補什麼

- 前一關是「我能碰到 device」
- 這一關是「device 主動通知我事件時，我能接住並清掉它」

## 看 source code 時先抓哪幾個點

先把 IRQ 當成「裝置敲門」來讀：

1. `dl_edu_irq_probe()`：沿用 `05` 的 PCI enable / BAR map，然後多申請 IRQ vector
2. `request_irq()`：把 `dl_edu_irq_handler()` 登記成中斷進來時的 handler
3. `iowrite32(... DL_EDU_IRQ_RAISE_REG)`：自我測試如何叫 EDU 觸發一次中斷
4. `dl_edu_irq_handler()`：handler 如何讀 status、判斷是不是自己的事件、寫 acknowledge
5. `complete()` / `wait_for_completion_timeout()`：probe 如何等待 handler 確認中斷真的發生
6. `dl_edu_irq_remove()`：卸載時先停 IRQ，再釋放 PCI resource

遇到 kernel API 時，先套用「參數角色」模板，完整方法見 [`../../docs/onboarding/kernel-api-parameter-roles.md`](../../docs/onboarding/kernel-api-parameter-roles.md)。

| API | 參數角色 | 第一輪理解 |
|---|---|---|
| `pci_set_master(pdev)` | PCI device | 允許裝置主動發起 bus transaction；MSI/某些主動通知路徑需要它。 |
| `pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES)` | device、最少/最多 vector、IRQ 類型 | 向 PCI core 要 1 條 IRQ vector，允許 MSI/MSI-X/INTx。 |
| `pci_irq_vector(pdev, 0)` | device、vector index | 取回第 0 條 IRQ vector number。 |
| `request_irq(dl->irq_vector, dl_edu_irq_handler, flags, name, dl)` | vector、handler、flags、名字、dev_id | 把 IRQ 接到 handler；`dl` 會傳回 handler 當 per-device state。 |
| `wait_for_completion_timeout(&dl->irq_done, timeout)` | completion、timeout | probe 等 handler 呼叫 `complete()`，確認 IRQ 真的抵達。 |

第一輪先記住：handler 裡不能只印 log，還必須把裝置端的中斷狀態清掉，否則可能一直重進。

## 第一次卡住先看哪裡

- handler 沒進來
  - 先確認你真的有觸發 interrupt raise register
- handler 一直重進
  - 優先懷疑 acknowledge 沒清乾淨
- 只想用 MSI，不想理 INTx
  - 先不要跳步；先把「handler 能正常清中斷」做對
