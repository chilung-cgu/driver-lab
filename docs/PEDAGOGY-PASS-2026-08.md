# Pedagogy pass — 2026-08

> Branch: `review/pedagogy-pass-2026-08`
>
> Base: `review/accuracy-audit-2026-08`
>
> Companion: `chilung-cgu/pcie-study:review/pedagogy-pass-2026-08`

## 結論

本輪從 accuracy audit branch 分出獨立 teaching branch：

```text
main
  └─ review/accuracy-audit-2026-08
       └─ review/pedagogy-pass-2026-08
```

不從舊 `main` 重寫，因為 audit branch 已修正 MMIO ordering、DMA address/ownership、IRQ teardown、
mmap consistency 等重要問題；從 main 出發會重新引入 semantic regression 風險。

不直接把教學改寫塞進 audit PR，因為 technical fixes 與 writing changes 混在同一個巨大 diff，會降低
reviewability。新的 branch 只做 beginner readability、structure、cross-repo terminology 與 teaching
validation，不改變 current `.c/.h/.sh` behavior。

## Pilot 範圍

1. `docs/concepts/pcie-primer.md`
2. `labs/05-pci-edu-mmio/README.md`
3. `labs/06-pci-edu-irq/README.md`
4. `labs/07-pci-edu-dma/README.md`

配套 `pcie-study`：P1-10、P2-07、P2-14。

這組文件涵蓋：

```text
PCI resource / mapping
→ MMIO access
→ interrupt notification
→ DMA address / ownership
→ ordering
→ posted arrival
→ operation completion
→ payload correctness
→ quiesce-before-free
```

## 原則

- Accuracy audit 是 correctness baseline；pedagogy pass 不得弱化其限制。
- 第一次出現的專有名詞要定義，並說「不代表什麼」。
- 先教完整 flow，再讀 source function / register。
- 每個 API 都寫清 resource、context、observer、lifetime。
- Test 結果分 static、compile、runtime、stress、fault-injection。
- QEMU EDU 是 teaching device，不包裝成 production accelerator driver。
- Source path / symbol 取代固定行號。
- `pcie-study` 與 `driver-lab` 使用同一組詞：ordering、arrival、completion、correctness、quiesce。

## Review 與合併順序

1. Audit PR 保持 Draft，完成 CI 與 Linux/QEMU runtime review。
2. Pedagogy branch 對 audit branch 開獨立 Draft PR。
3. 先 review technical baseline 有沒有被文字改寫弱化。
4. 再由初學者視角 review 名詞、段落順序與 examples。
5. 先合併 `driver-lab` audit PR，再合併 `pcie-study` audit PR。
6. Pedagogy PR rebase / retarget 到新 main，重跑 CI。
7. 先合併 `driver-lab` pedagogy PR，再更新 `pcie-study` source link / SHA 並合併。
8. 最後才重新生成 NotebookLM artifacts。

## 後續批次

### Batch 1：Lab00～04

Module lifecycle、debugfs、char device、ioctl/poll/mmap、race/mutex。

### Batch 2：Lab05～07

本次 pilot；完成 MMIO、IRQ、DMA 主線。

### Batch 3：Lab08～09 與 QEMU runbook

Userspace runtime、stress/fault injection、host/guest/cross-architecture。

### Batch 4：Companion regeneration

只從最終 merged current source 重新產生 `.c.md/.h.md/.sh.md`，人工 review 後再列為 reviewed。

## 驗收

- Standard、template、manifest、structure CI 存在；
- Pilot 文件全部使用一致 teaching structure；
- Source code 未因本輪文字改寫而變動；
- Current test 能/不能證明的範圍寫清楚；
- Runtime 尚未完成的項目保留；
- Draft PR base 為 audit branch，不直接 merge；
- audit branch 與 main 不被覆蓋。
