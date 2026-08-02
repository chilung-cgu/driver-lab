# Docs Index

`docs/`依用途分成 onboarding、concepts、guides、reference、workflow。第一次閱讀時，先走入口文件，不要把所有companion docs一次打開。

## 權威順序

若文件互相衝突：

1. Linux/QEMU官方文件與實際觀測；
2. current source與current tests；
3. Lab README、reviewed guides與[`reference/accuracy-audit-2026-08.md`](reference/accuracy-audit-2026-08.md)；
4. generated或line-by-line companion `.c.md/.h.md/.sh.md`。

Companion適合旁讀，但可能尚未隨source同步。

## 入口文件

| 文件 | 何時看 | 回答什麼 |
|---|---|---|
| [`onboarding/reading-map.md`](onboarding/reading-map.md) | 準備開始 | 今天先讀哪裡、何時進下一階段 |
| [`onboarding/learning-dashboard.md`](onboarding/learning-dashboard.md) | 每完成一關 | 成功訊號與前進條件 |
| [`onboarding/beginner-primer.md`](onboarding/beginner-primer.md) | 完全新手 | kernel/module/driver最低心智模型 |
| [`onboarding/kernel-filesystem-surfaces.md`](onboarding/kernel-filesystem-surfaces.md) | 分不清 `/dev`、sysfs、procfs、debugfs | filesystem入口角色 |
| [`onboarding/kernel-api-parameter-roles.md`](onboarding/kernel-api-parameter-roles.md) | 開始讀kernel API | input/output/resource/callback/cleanup參數角色 |
| [`concepts/pcie-primer.md`](concepts/pcie-primer.md) | 進Lab05前 | PCI/BAR/MMIO/IRQ/DMA的correctness-first地圖 |
| [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md) | 進Lab04 | race、mutex、kthread、lifetime閱讀順序 |
| [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md) | 進Lab05 | host/guest、bind、BAR validation、MMIO閱讀順序 |
| [`reference/companion-docs-index.md`](reference/companion-docs-index.md) | trace某份source | companion doc位置 |
| [`reference/source-index.md`](reference/source-index.md) | 要查官方定義 | kernel/QEMU直接來源 |

## 目錄分類

- [`onboarding/`](onboarding/)：起步、環境、術語、lab transition。
- [`concepts/`](concepts/)：核心概念前導。
- [`guides/`](guides/)：study order、walkthrough、runbook、checklist。
- [`reference/`](reference/)：audit、來源、除錯、companion索引。
- [`workflow/`](workflow/)：agent/Git流程，不是driver學習主線。

## 完全新手順序

1. [`onboarding/reading-map.md`](onboarding/reading-map.md)
2. [`onboarding/learning-dashboard.md`](onboarding/learning-dashboard.md)
3. [`onboarding/beginner-primer.md`](onboarding/beginner-primer.md)
4. [`onboarding/lab-file-roles.md`](onboarding/lab-file-roles.md)
5. [`onboarding/linux-host-setup.md`](onboarding/linux-host-setup.md)
6. [`onboarding/check-kernel-env-explained.md`](onboarding/check-kernel-env-explained.md)
7. Lab00 → [`onboarding/00-to-01-debugfs-bridge.md`](onboarding/00-to-01-debugfs-bridge.md) → Lab01
8. [`onboarding/01-to-03-user-kernel-abi-bridge.md`](onboarding/01-to-03-user-kernel-abi-bridge.md) → Lab02/03
9. [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md) → Lab04
10. [`concepts/pcie-primer.md`](concepts/pcie-primer.md) + [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md) → Lab05
11. [`onboarding/05-to-07-pci-irq-dma-bridge.md`](onboarding/05-to-07-pci-irq-dma-bridge.md) → Lab06/07
12. [`onboarding/07-to-09-runtime-validation-bridge.md`](onboarding/07-to-09-runtime-validation-bridge.md) → Lab08/09

## QEMU文件

- Host入口：[`../qemu/README.md`](../qemu/README.md)
- ARM host / x86_64 guest：[`../qemu/arm-host-x86-guest.md`](../qemu/arm-host-x86-guest.md)
- 第一次概觀：[`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- Guest runbook：[`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- Guest速查：[`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)

## Review邊界

- Audit branch已新增static/build CI與更嚴格tests。
- 真正`insmod/rmmod`、MMIO、IRQ、DMA與sanitizer證據仍需Linux/QEMU guest執行。
- 不要因為Markdown完整或module可compile，就宣稱runtime correctness已證明。
