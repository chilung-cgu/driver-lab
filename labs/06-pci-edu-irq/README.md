# 06 - PCI EDU IRQ

## 目標

在 `edu` 裝置上補上 interrupt path。

> [!NOTE]
> 這一關現在已經有第一版可 build 的 driver code 與 smoke test。
> 真正的載入與驗證仍必須在 Linux guest 內完成。

## 開始前先看

- [`../../docs/pcie-primer.md`](../../docs/pcie-primer.md)
- [`../../docs/qemu-edu-first-pass.md`](../../docs/qemu-edu-first-pass.md)

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
driver_lab_edu: request_irq ok
driver_lab_edu: irq status=0x1
driver_lab_edu: irq acknowledged
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

## 新手先記住這一關在補什麼

- 前一關是「我能碰到 device」
- 這一關是「device 主動通知我事件時，我能接住並清掉它」

## 第一次卡住先看哪裡

- handler 沒進來
  - 先確認你真的有觸發 interrupt raise register
- handler 一直重進
  - 優先懷疑 acknowledge 沒清乾淨
- 只想用 MSI，不想理 INTx
  - 先不要跳步；先把「handler 能正常清中斷」做對
