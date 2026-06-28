# Docs Index

`docs/` 現在依用途分成五類，目的是讓新手先找到該看的文件，再決定要不要深入後面的材料。

## 這份索引和其他入口差在哪

| 文件 | 你什麼時候看 | 它回答什麼 |
|---|---|---|
| 這份 `docs/README.md` | 不知道文件放哪裡時 | `docs/` 目錄有哪些類別、各自放什麼 |
| [`onboarding/reading-map.md`](onboarding/reading-map.md) | 準備開始讀 repo 時 | 今天到底先看哪幾份、什麼時候才看後面章節 |
| [`onboarding/learning-dashboard.md`](onboarding/learning-dashboard.md) | 每完成一關要判斷能否前進時 | 每個階段要跑什麼、成功訊號是什麼、前進條件是什麼 |
| [`onboarding/beginner-glossary.md`](onboarding/beginner-glossary.md) | 遇到陌生名詞時 | 單一查詢表，不需要從頭背完 |
| [`onboarding/kernel-filesystem-surfaces.md`](onboarding/kernel-filesystem-surfaces.md) | 開始分不清 `/dev`、`/sys`、`/proc`、debugfs 時 | driver 建立或暴露的 filesystem 入口各自代表什麼 |
| [`onboarding/kernel-api-parameter-roles.md`](onboarding/kernel-api-parameter-roles.md) | 開始讀 `.c` 裡的 kernel API 呼叫時 | 每個參數是 input、output、resource、數量、名字、callback，還是 cleanup 對象 |
| [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md) | 準備從 `03` 進 `04-locking-and-races` 時 | Lab04 的前置條件、閱讀順序、source trace 順序與驗收方式 |
| [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md) | 準備從 `04` 進 `05-pci-edu-mmio` 時 | Lab05 的 host/guest 邊界、閱讀順序、source trace 順序與驗收方式 |
| [`reference/companion-docs-index.md`](reference/companion-docs-index.md) | 正在 trace 某份 source 時 | 哪些 source 檔旁邊有可直接閱讀的 companion doc |
| [`reference/companion-docs-rollout-plan.md`](reference/companion-docs-rollout-plan.md) | 想知道 companion docs 會怎麼分段補齊時 | 哪些檔案需要長篇旁讀、哪些先不需要 |

## 目錄分類

- [`onboarding/`](onboarding/)
  - 新手起步、環境、術語、閱讀順序
- [`concepts/`](concepts/)
  - 核心概念前導，例如併發、PCIe、AI 加速卡 driver 心智模型
- [`guides/`](guides/)
  - 操作 runbook、學習路線、walkthrough、checklist
- [`reference/`](reference/)
  - 除錯圖鑑、閱讀索引、companion docs index、playbook、參考資料
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
9. 完成 `01` 後，讀 [`onboarding/lab-transition-map.md`](onboarding/lab-transition-map.md) 與 [`onboarding/01-to-03-user-kernel-abi-bridge.md`](onboarding/01-to-03-user-kernel-abi-bridge.md)
10. 開始讀 `02` source 前，讀 [`onboarding/kernel-filesystem-surfaces.md`](onboarding/kernel-filesystem-surfaces.md) 與 [`onboarding/kernel-api-parameter-roles.md`](onboarding/kernel-api-parameter-roles.md)
11. 進 `04` 前，讀 [`onboarding/03-to-05-concurrency-pci-bridge.md`](onboarding/03-to-05-concurrency-pci-bridge.md) 與 [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md)
12. 進 `05` 前，讀 [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md)，用它把 PCI primer、QEMU bring-up、Lab05 README、source companion doc 與 smoke test 串起來
13. 進 `06/07` 前，讀 [`onboarding/05-to-07-pci-irq-dma-bridge.md`](onboarding/05-to-07-pci-irq-dma-bridge.md)
14. 進 `08/09` 前，讀 [`onboarding/07-to-09-runtime-validation-bridge.md`](onboarding/07-to-09-runtime-validation-bridge.md)

## QEMU 相關文件在哪裡

- 概念前導：[`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- Lab05 導讀：[`guides/lab-05-study-order.md`](guides/lab-05-study-order.md)
- guest 操作手冊：[`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- guest 速查單：[`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)
- host 端 QEMU 入口：[`../qemu/README.md`](../qemu/README.md)
