# Docs Index

`docs/` 現在依用途分成五類，目的是讓新手先找到該看的文件，再決定要不要深入後面的材料。

## 目錄分類

- [`onboarding/`](onboarding/)
  - 新手起步、環境、術語、閱讀順序
- [`concepts/`](concepts/)
  - 核心概念前導，例如併發、PCIe、AI 加速卡 driver 心智模型
- [`guides/`](guides/)
  - 操作 runbook、學習路線、walkthrough、checklist
- [`reference/`](reference/)
  - 除錯圖鑑、閱讀索引、playbook、參考資料
- [`workflow/`](workflow/)
  - agent / Git workflow 規範，不是 driver 學習主線

## 完全新手建議順序

1. [`onboarding/reading-map.md`](onboarding/reading-map.md)
2. [`onboarding/learning-dashboard.md`](onboarding/learning-dashboard.md)
3. [`onboarding/beginner-primer.md`](onboarding/beginner-primer.md)
4. [`onboarding/lab-file-roles.md`](onboarding/lab-file-roles.md)
5. [`onboarding/linux-host-setup.md`](onboarding/linux-host-setup.md)
6. [`onboarding/check-kernel-env-explained.md`](onboarding/check-kernel-env-explained.md)
7. 回到 repo 根目錄，開始做 `labs/00-hello-module`
8. 完成 `00` 後，先讀 [`onboarding/00-to-01-debugfs-bridge.md`](onboarding/00-to-01-debugfs-bridge.md)，再進 `labs/01-debugfs-logging`
9. 完成 `01` 後，讀 [`onboarding/lab-transition-map.md`](onboarding/lab-transition-map.md) 與 [`onboarding/01-to-03-user-kernel-abi-bridge.md`](onboarding/01-to-03-user-kernel-abi-bridge.md)，再進 `02/03`
10. 進 `04/05` 前，讀 [`onboarding/03-to-05-concurrency-pci-bridge.md`](onboarding/03-to-05-concurrency-pci-bridge.md)
11. 進 `06/07` 前，讀 [`onboarding/05-to-07-pci-irq-dma-bridge.md`](onboarding/05-to-07-pci-irq-dma-bridge.md)
12. 進 `08/09` 前，讀 [`onboarding/07-to-09-runtime-validation-bridge.md`](onboarding/07-to-09-runtime-validation-bridge.md)

## QEMU 相關文件在哪裡

- 概念前導：[`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- guest 操作手冊：[`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- guest 速查單：[`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)
- host 端 QEMU 入口：[`../qemu/README.md`](../qemu/README.md)
