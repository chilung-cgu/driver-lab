# 06 - PCI EDU IRQ

## 目標

在 `edu` 裝置上補上 interrupt path。

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

## 新手先記住這一關在補什麼

- 前一關是「我能碰到 device」
- 這一關是「device 主動通知我事件時，我能接住並清掉它」
