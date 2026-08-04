# Driver lab 教材品質標準

> 適用範圍：主要 onboarding、concept guide、各 Lab README、debug checklist 與 walkthrough。
>
> 本標準建立在 `review/accuracy-audit-2026-08` 的 correctness baseline 上。目標不是把 code
> 說得更簡單而已，而是讓初學者能知道每段 code 在解哪一個問題、能證明什麼、不能證明什麼。

## 先講結論

一份合格的 Lab README 必須把下列四層串起來：

```text
概念心智模型
→ current source 的 resource / context / lifetime
→ 可重現的 test 與 observable evidence
→ 失敗、限制與尚未驗證項目
```

只解釋 happy-path API 不夠；只貼 code 也不夠；只看到 test pass 更不能推論 production correctness。

## 讀者假設

預設讀者具備一般 C / Linux 使用經驗，但沒有 kernel driver、PCIe、MMIO、IRQ、DMA 實務。
專有名詞第一次出現必須立即定義，包含：

- probe / remove / bind；
- resource / mapping / ownership；
- execution context / hard IRQ；
- quiesce / in-flight / teardown；
- BAR / MMIO / posted write / read-back；
- DMA address / coherent / streaming / descriptor / doorbell。

## 每份 Lab README 的固定層次

1. **先講結論**：這關建立哪個最小閉環。
2. **不確定處與驗證狀態**：static、compile、runtime、fault-injection 分開。
3. **這一關要解決什麼問題**：從失敗案例開始。
4. **名詞先說清楚**：定義與「不代表什麼」。
5. **心智模型**：用圖或比喻建立第一輪理解，並標出邊界。
6. **先備 gate**：環境不成立時，不要先改 driver source。
7. **Resource / data flow**：setup、normal path、error unwind、teardown。
8. **從簡單到精確**：逐步讀 current source，不先灌滿所有例外。
9. **最小正確範式**：指出 context、contract、observer、lifetime。
10. **看似合理但錯誤的寫法**：說明症狀與修正。
11. **如何執行與觀察**：command、expected evidence、不能推出的結論。
12. **Debug order**：依 dependency 排查，不隨機試 API。
13. **常見誤解**：完整寫出 why wrong / correct statement。
14. **適用邊界與尚未驗證**：QEMU EDU 不等於 production hardware。
15. **第一次閱讀先記住**：5～8 個核心句。
16. **Self-check + 頁內答案**：測 context、contract、limit。
17. **來源與查證**：官方文件、current source、test。

## Source 解說規則

### 不綁固定行號

使用 file path、function、symbol、register name。固定行號在 source 修改後容易漂移。

### 每個 API 都說明角色

例如：

```text
pci_request_region()：取得 BAR resource ownership
pci_iomap()          ：建立 I/O mapping
```

不能只寫「接著 call 這兩個 API」。

### 同時說正常路徑與 failure unwind

每取得一個 resource，就回答：

- 何時開始 live？
- 誰可能使用它？
- 失敗時由誰撤銷？
- teardown 前要先停哪個 producer / in-flight user？

### 不把 teaching shortcut 寫成通用規則

例如 QEMU EDU Lab07 的 single-buffer command path，不應被改寫成「所有 DMA doorbell 前都固定
`dma_wmb(); wmb(); writel();`」。要明確說出它不是 descriptor ring。

## Evidence 分級

| 層級 | 能證明什麼 | 不能證明什麼 |
|---|---|---|
| Markdown / source review | contract 與 code 意圖已檢查 | module 能正常執行 |
| Compile / static check | target headers 下可編譯、部分靜態規則通過 | MMIO/IRQ/DMA runtime 正確 |
| Smoke runtime | 指定環境的正常路徑可重現 | race、timeout、所有 architecture |
| Stress / sanitizer | 特定競態與 memory bug 的證據增加 | 數學上證明完全無 bug |
| Fault injection | error path / recovery 在指定 fault 下可重現 | 真實硬體所有 failure mode |

README 必須使用正確層級描述，不把較低證據升格。

## 用語規則

- 第一次出現使用中文（English）與縮寫展開。
- 句子寫清楚誰執行、誰觀察、什麼 context、什麼 resource。
- 「完成」要指出是 submission、posted arrival、operation completion 還是 payload correctness。
- 「同步」要指出是 mutual exclusion、memory ordering、handler synchronization 或 device quiesce。
- 不用「一定」「全部」「直接」等絕對詞，除非 contract 的 scope 已明確。

## Review gates

### Gate 1：技術正確性

- 保留 accuracy audit 修正；
- 對照 official docs / current source；
- architecture / device-specific內容有標示；
- 不虛構 runtime 結果。

### Gate 2：初學者可讀性

- 名詞先定義；
- 先有整體 flow，再進 source；
- 一段不同時引入過多新概念；
- code 前先說要解什麼問題；
- 第一輪不需查大量外部文件才看得懂。

### Gate 3：可驗證性

- 明確 command 與 evidence；
- 說明 test 能/不能證明什麼；
- 有 dependency-based debug order；
- error / teardown path 可追蹤。

### Gate 4：維護性

- 不依賴固定行號；
- source / test path 存在；
- Self-check 有頁內答案；
- official links 可用；
- migrated manifest 與 structure check 通過。

## Pilot migration

第一批先處理：

1. `docs/concepts/pcie-primer.md`
2. `labs/05-pci-edu-mmio/README.md`
3. `labs/06-pci-edu-irq/README.md`
4. `labs/07-pci-edu-dma/README.md`

它們與 `pcie-study` 的 P1-10、P2-07、P2-14 共同建立：

```text
resource / mapping
→ ordering
→ posted arrival
→ device completion
→ payload correctness
→ quiesce-before-free
```

Pilot 經人工 review 與 runtime gate 後，再依 Lab00→09 擴大。

## 完成定義

文件只有在以下條件都成立時，才能列入 pedagogy-reviewed manifest：

- 技術語意未比 audit branch 退步；
- 必要術語有定義；
- 有結論、問題、心智模型與精確 contract；
- 有 resource/data flow、正反範例與 evidence；
- 有 failure / teardown / limits；
- 有 Self-check 與官方來源；
- structure check 通過；
- 至少一次 technical + beginner readability review；
- 涉及 runtime 行為時，有 log，或清楚標為待驗證。

模板與 CI 只能檢查結構，不能自動證明 kernel / device 語意或 runtime correctness。

## 官方查證入口

- Linux PCI guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- MSI guide: <https://docs.kernel.org/PCI/msi-howto.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
