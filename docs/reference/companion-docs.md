# Companion documents — 旁讀層，不是第二套主教材

> **定位**：說明 source 旁的 `*.c.md`、`*.h.md`、`*.sh.md`、`Makefile.md` 如何使用、何時可信，以及為何不再維護一份巨大逐檔索引與 rollout plan。

## 先講結論

主線只有：

```text
Lab README / canonical guide
→ current source and test
→ 必要時開同名 companion
→ 結論回到 source、official docs、runtime evidence
```

Companion 通常與 source 放在同一目錄並採 `<source-file>.md` 命名，因此不需要另一份數百行索引重複列出所有檔案。用 repository search 或直接找同名 `.md` 即可。

## 不確定處與驗證狀態

- 既有 companions 多數由先前 AI 生成，可能包含舊行號、舊 source behavior 或過度簡化。
- 它們沒有因 pedagogy pass 自動取得 `reviewed` 狀態。
- Generated doc 與 current source 不一致時，永遠以 current source/test/audit/official docs 為準。

## 何時適合開 companion

- 第一次 trace 一個較長 function，不知道 call flow。
- 想知道某個 Makefile/test script 的每段用途。
- 已先看 README 和 source，但需要逐段旁讀。

不適合：

- 用 companion 取代 current source。
- 依固定行號判斷 current behavior。
- 從 companion 的絕對句推導 kernel/device contract。
- 一開始同時打開所有 generated files。

## Authority order

1. Target kernel/QEMU/device reproduced behavior。
2. Official Linux/QEMU/device documentation。
3. Current `.c/.h/.sh` source and tests。
4. Current Lab README / reviewed canonical guides。
5. Generated companion documents。

## Reviewed companion 的最低條件

一份 companion 只有在以下條件成立後才能標為 reviewed：

- 指向 immutable source SHA 或與 current source 自動比對；
- function/symbol/resource/lifetime 與 current implementation 一致；
- 沒有把 QEMU/device-specific behavior 寫成通則；
- 正確區分 static、compile、runtime evidence；
- technical reviewer 與 beginner readability reviewer 都通過；
- source 改動後有 stale detection 或重新生成流程。

## 目前策略

- 不再繼續人工新增每一個小 wrapper 的 companion。
- 優先把主 README、concepts、runbook、tests 與 source comments 維持正確。
- 高價值 companion 可在 audit/main 合併、runtime logs 完整後重新生成。
- Rollout 計畫已收斂到這份政策，不再保留另一份容易過期的清單。

## 如何找到同名 companion

```sh
find labs runtime tests scripts qemu -name '*.md' | sort
find labs/07-pci-edu-dma -maxdepth 1 -name 'driver_lab_edu_dma.c*'
```

例如：

```text
labs/07-pci-edu-dma/driver_lab_edu_dma.c
labs/07-pci-edu-dma/driver_lab_edu_dma.c.md
```

## 來源與查證

- Current source tree and tests in this repository。
- Teaching standard: [`../TEACHING-QUALITY-STANDARD.md`](../TEACHING-QUALITY-STANDARD.md)
- Accuracy audit: [`accuracy-audit-2026-08.md`](accuracy-audit-2026-08.md)
