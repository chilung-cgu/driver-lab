# AI 加速卡 Host Driver 架構對映

## 這份文件的目的

你現在做的 labs 不只是一般 Linux driver 練習，它們各自對應到未來 AI 加速卡 Host Driver 的某一塊能力。

## 一般 AI 加速卡 Host Driver 通常包含什麼

- PCI device discovery / `probe`
- BAR 映射與 MMIO register access
- IRQ / MSI / MSI-X
- DMA buffer 管理
- firmware loading
- command submission
- completion handling
- user-space runtime 介面
- debug / trace / recovery / reset

## 本專案各 lab 的對映

| Lab | 你實際在練什麼 | 對應到 AI 加速卡工作 |
|---|---|---|
| `00-hello-module` | 最小 build/load/unload 閉環 | driver 生命週期基本功 |
| `01-debugfs-logging` | 觀測、狀態導出、debug path | bring-up / debug 能力 |
| `02-char-device` | 最小 user-kernel 邊界 | runtime 與 driver 的第一層接口 |
| `03-ioctl-poll-mmap` | 正式控制面 / 資料面介面 | command、event、shared buffer |
| `04-locking-and-races` | 併發與 lifetime | queue / IRQ / worker 協調 |
| `05-pci-edu-mmio` | PCI probe + BAR + MMIO | 裝置初始化與 register 操作 |
| `06-pci-edu-irq` | interrupt handling | completion / event path |
| `07-pci-edu-dma` | DMA programming | input/output buffer 搬運 |
| `08-runtime-library` | user-space 封裝 | `libfoo_rt.so` 類型 runtime |
| `09-stress-and-fault-injection` | regression / error path | 產品穩定性與 bring-up 品質 |

## 你暫時還沒練到、但真卡會遇到的 20%

- vendor-specific register map
- firmware boot protocol
- 真實 MSI-X vector 配置
- reset / FLR / AER
- IOMMU / PASID / SVA / SR-IOV
- cache coherency 與平台怪 bug
- 真實效能瓶頸

## 面試時可以怎麼客觀描述

可以這樣講：

> 我還沒有 vendor 真卡 bring-up 經驗，但我已經把 Linux host driver 的共通骨架拆成可反覆驗證的 labs，包含 module lifecycle、user-kernel API、debugfs/dynamic debug、char device、以及後續會用 QEMU EDU 練 PCI/MMIO/IRQ/DMA。這讓我在拿到實體硬體與資料手冊後，不會從 0 開始學 Linux driver 本身，而是只需要補 vendor-specific 部分。

