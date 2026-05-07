# 06 - PCI EDU IRQ

## 目標

在 `edu` 裝置上補上 interrupt path。

## 開始前先看

- [`../../docs/pcie-primer.md`](../../docs/pcie-primer.md)

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

## 第一次驗收時你要看到什麼

- `request_irq()` 成功
- handler 有 log
- 同一個中斷不會無限重進

## 新手先記住這一關在補什麼

- 前一關是「我能碰到 device」
- 這一關是「device 主動通知我事件時，我能接住並清掉它」
