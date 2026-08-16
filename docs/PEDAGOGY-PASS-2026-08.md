# Pedagogy pass — beginner-first、correctness-preserving

## 先講結論

本 branch 建立在 `review/accuracy-audit-2026-08` 上，已完成：

- Labs00～09 全部 primary README 的 beginner-first 改寫；
- PCIe、concurrency、accelerator architecture 三份核心 concept；
- 單一 START-HERE、環境、kernel interfaces；
- debugging 與 companion 政策集中化；
- 重複 onboarding/bridge/roadmap/reference 文件整併；
- structure、local-link、docs-architecture、static/build CI。

目標不是把內容說得簡單而已，而是讓讀者能由：

```text
心智模型
→ current source / resource / context / lifetime
→ test evidence
→ failure / limits
```

逐步建立可驗證理解。

## Technical baseline

Pedagogy 改寫不得撤銷 accuracy audit 的訂正，包括：

- syscall/IRQ entry 不等於 task switch；
- wakeup 不等於 predicate 成立；
- `READ_ONCE/WRITE_ONCE` 不等於 general barrier/lock；
- BAR raw/resource/`__iomem` 分離；
- normal MMIO ordering、posted arrival、device completion 分離；
- MSI/MSI-X 是 Memory Write Request；
- CPU pointer、DMA address、device-local address 分離；
- coherent/streaming ownership 與 ordering/completion/lifetime 分離；
- teardown 先 quiesce/synchronize，再 free。

## Evidence status

### 已完成

- source/document alignment review；
- all Lab README teaching structure；
- canonical docs consolidation；
- ShellCheck/Markdown/local link/checkpatch；
- userspace runtime/CLI build；
- Labs00～07 external-module compile；
- pedagogy/docs-architecture checks。

### 仍待完成

- target Linux runtime：Labs00～04；
- QEMU EDU runtime：Labs05～07；
- Lab03/04 concurrency + sanitizer；
- Lab06 repeated IRQ/teardown；
- Lab07 timeout/reset/IOMMU/SWIOTLB；
- real hardware/device-specific validation；
- generated companion regeneration/review。

## Canonical reader path

1. [`onboarding/START-HERE.md`](onboarding/START-HERE.md)
2. Each Lab `README.md`
3. Current source/test
4. Needed concept/study-order/runbook
5. [`reference/debugging.md`](reference/debugging.md) on failure
6. Companion only as secondary side reading

## Maintenance rule

A new top-level teaching document must answer a distinct reader question. Do not add another roadmap/bridge/checklist if existing canonical docs can absorb the content. New Lab details belong first in that Lab README; cross-lab theory belongs in one concept; operational repetition belongs in a walkthrough/checklist pair only when first-run and repeat-run use cases are genuinely different.

## Merge order

Keep this PR based on the audit branch until audit runtime/review is complete. After audit merges, rebase/retarget, rerun all gates, then merge `driver-lab` pedagogy before the companion `pcie-study` pedagogy PR.
