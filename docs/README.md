# Driver-lab documentation — canonical map

## 先講結論

`docs/` 現在只保留少數 canonical entry points；不再要求初學者在十多份 onboarding/bridge 文件間跳轉。

```text
START-HERE
→ Lab README
→ concept / study-order guide（需要時）
→ current source + test
→ debugging/reference（失敗或查詞時）
```

## 新手入口

1. [`onboarding/START-HERE.md`](onboarding/START-HERE.md)
2. [`onboarding/linux-environment.md`](onboarding/linux-environment.md)
3. [`onboarding/kernel-interfaces.md`](onboarding/kernel-interfaces.md)
4. Lab00 → Lab09 的各自 `README.md`

## 核心 concepts

- [`concepts/concurrency-primer.md`](concepts/concurrency-primer.md)：shared state、locks、ordering、waiting、lifetime。
- [`concepts/pcie-primer.md`](concepts/pcie-primer.md)：PCI/BAR/MMIO/IRQ/DMA correctness-first 地圖。
- [`concepts/accelerator-driver-architecture.md`](concepts/accelerator-driver-architecture.md)：labs 如何對映到 accelerator software stack。

## Study-order / runbooks

- [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md)
- [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md)
- [`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- [`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- [`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)

Walkthrough 用於第一次操作；checklist 只在已理解後重跑，不取代理解。

## Reference

- [`reference/glossary.md`](reference/glossary.md)：精簡、分層、可搜尋的術語速查；不取代主教材。
- [`reference/debugging.md`](reference/debugging.md)：code reading、common failures、tools、bug diary、regression。
- [`reference/source-index.md`](reference/source-index.md)：official source entry points。
- [`reference/companion-docs.md`](reference/companion-docs.md)：generated companion 使用政策。
- [`reference/accuracy-audit-2026-08.md`](reference/accuracy-audit-2026-08.md)：技術訂正與 runtime gaps。

## Pedagogy / maintenance

- [`TEACHING-QUALITY-STANDARD.md`](TEACHING-QUALITY-STANDARD.md)
- [`PEDAGOGY-PASS-2026-08.md`](PEDAGOGY-PASS-2026-08.md)
- [`templates/LAB-README-TEMPLATE.md`](templates/LAB-README-TEMPLATE.md)
- [`pedagogy/migrated-docs.txt`](pedagogy/migrated-docs.txt)
- [`pedagogy/canonical-docs.txt`](pedagogy/canonical-docs.txt)

## Authority order

1. Target Linux/QEMU/device runtime evidence。
2. Official documentation。
3. Current source/tests。
4. Reviewed README/concepts/guides。
5. Glossary / generated companions。

Static/compile/smoke/stress/fault/real-hardware evidence 必須分開描述。

## Meta workflow

[`workflow/ai-agent-git-checkpoint-policy.md`](workflow/ai-agent-git-checkpoint-policy.md) 是 repo maintenance policy，不是 driver 學習主線。
