# Docs Index — 先選入口，不要一次打開所有文件

`docs/`依用途分成 onboarding、concepts、guides、reference、workflow。完全初學者先走reading map與
beginner primer；進PCI前再讀PCIe primer。Generated companion只作旁讀，不是primary authority。

## 先講結論

目前已完成新的 beginner-first pilot：

1. [`concepts/pcie-primer.md`](concepts/pcie-primer.md)
2. [`../labs/05-pci-edu-mmio/README.md`](../labs/05-pci-edu-mmio/README.md)
3. [`../labs/06-pci-edu-irq/README.md`](../labs/06-pci-edu-irq/README.md)
4. [`../labs/07-pci-edu-dma/README.md`](../labs/07-pci-edu-dma/README.md)

配套規則：

- [`TEACHING-QUALITY-STANDARD.md`](TEACHING-QUALITY-STANDARD.md)
- [`templates/LAB-README-TEMPLATE.md`](templates/LAB-README-TEMPLATE.md)
- [`PEDAGOGY-PASS-2026-08.md`](PEDAGOGY-PASS-2026-08.md)
- [`pedagogy/migrated-docs.txt`](pedagogy/migrated-docs.txt)

其他文件仍以accuracy audit為正確性基線，將依Lab順序分批改寫。

## 權威順序

若文件互相衝突：

1. Target Linux/QEMU實際觀測與官方文件；
2. Current source與current tests；
3. Lab README、reviewed guides與[`reference/accuracy-audit-2026-08.md`](reference/accuracy-audit-2026-08.md)；
4. Generated或line-by-line companion `.c.md/.h.md/.sh.md`。

Companion適合旁讀，但可能尚未隨source同步。

## 完全初學者入口

| 文件 | 何時看 | 回答什麼 |
|---|---|---|
| [`onboarding/reading-map.md`](onboarding/reading-map.md) | 準備開始 | 現在先看哪裡、做到什麼才能前進 |
| [`onboarding/learning-dashboard.md`](onboarding/learning-dashboard.md) | 每完成一關 | 成功訊號與能力gate |
| [`onboarding/beginner-primer.md`](onboarding/beginner-primer.md) | 完全新手 | kernel/module/driver最低心智模型 |
| [`onboarding/lab-file-roles.md`](onboarding/lab-file-roles.md) | 第一次看repo | `.c`、Makefile、test、UAPI各扮演什麼角色 |
| [`onboarding/kernel-filesystem-surfaces.md`](onboarding/kernel-filesystem-surfaces.md) | 分不清 `/dev`、sysfs、procfs、debugfs | filesystem入口角色 |
| [`onboarding/kernel-api-parameter-roles.md`](onboarding/kernel-api-parameter-roles.md) | 開始讀kernel API | input/output/resource/callback/cleanup參數角色 |
| [`concepts/pcie-primer.md`](concepts/pcie-primer.md) | 進Lab05前 | PCI/BAR/MMIO/IRQ/DMA的完整地圖 |

## 建議閱讀順序

```text
reading map
→ learning dashboard
→ beginner primer
→ Lab00～02
→ Lab03 ABI paths
→ Lab04 concurrency/lifetime
→ PCIe primer
→ Lab05 MMIO
→ Lab06 IRQ
→ Lab07 DMA
→ Lab08 runtime
→ Lab09 stress scaffold
```

對應文件：

1. [`onboarding/00-to-01-debugfs-bridge.md`](onboarding/00-to-01-debugfs-bridge.md)
2. [`onboarding/01-to-03-user-kernel-abi-bridge.md`](onboarding/01-to-03-user-kernel-abi-bridge.md)
3. [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md)
4. [`onboarding/03-to-05-concurrency-pci-bridge.md`](onboarding/03-to-05-concurrency-pci-bridge.md)
5. [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md)
6. [`onboarding/05-to-07-pci-irq-dma-bridge.md`](onboarding/05-to-07-pci-irq-dma-bridge.md)
7. [`onboarding/07-to-09-runtime-validation-bridge.md`](onboarding/07-to-09-runtime-validation-bridge.md)

## 三輪讀法

每個Lab：

```text
第一輪：README結論、問題、名詞、心智模型
第二輪：resource/data flow、source、error/teardown、反例
第三輪：執行test、保存evidence、做一個失敗實驗、回答Self-check
```

不要先打開所有companion docs。Current source與primary README足以完成第一輪。

## 目錄分類

- [`onboarding/`](onboarding/)：起步、環境、術語、lab transition。
- [`concepts/`](concepts/)：核心概念前導。
- [`guides/`](guides/)：study order、walkthrough、runbook、checklist。
- [`reference/`](reference/)：audit、來源、除錯、companion索引。
- [`workflow/`](workflow/)：agent/Git流程，不是driver學習主線。
- [`templates/`](templates/)：新教材格式，不是學習主線。
- [`pedagogy/`](pedagogy/)：已完成教學migration的manifest。

## QEMU / Guest入口

- Host入口：[`../qemu/README.md`](../qemu/README.md)
- ARM host / x86_64 guest：[`../qemu/arm-host-x86-guest.md`](../qemu/arm-host-x86-guest.md)
- 第一次概觀：[`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- Guest walkthrough：[`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- Guest checklist：[`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)

## Review / source入口

- Accuracy audit：[`reference/accuracy-audit-2026-08.md`](reference/accuracy-audit-2026-08.md)
- Pedagogy plan：[`PEDAGOGY-PASS-2026-08.md`](PEDAGOGY-PASS-2026-08.md)
- Teaching standard：[`TEACHING-QUALITY-STANDARD.md`](TEACHING-QUALITY-STANDARD.md)
- Official source index：[`reference/source-index.md`](reference/source-index.md)
- Companion index：[`reference/companion-docs-index.md`](reference/companion-docs-index.md)

## 驗證邊界

- Audit branch新增static/build CI與更嚴格tests。
- Pedagogy branch新增文件結構CI。
- 真正 `insmod/rmmod`、MMIO、IRQ、DMA與sanitizer evidence仍需Linux/QEMU guest。
- Markdown完整、module可compile、或一次smoke pass，都不能被描述成production correctness。
