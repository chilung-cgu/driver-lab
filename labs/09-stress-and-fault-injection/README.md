# 09 — Stress、fault injection 與可信的 regression oracle

> **定位**：Lab09 的目標是把『正常跑過一次』提升為可重複驗證。Stress 放大 timing/resource 問題，sanitizer 增加特定 bug class 的可見性，fault injection 驗 error/teardown；每個 test 都必須有會可靠失敗的 oracle。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab09 的目標是把『正常跑過一次』提升為可重複驗證。Stress 放大 timing/resource 問題，sanitizer 增加特定 bug class 的可見性，fault injection 驗 error/teardown；每個 test 都必須有會可靠失敗的 oracle。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current scaffold 主要覆蓋 Lab03 parallel/reload；Lab06 repeated IRQ、Lab07 timeout/reset/IOMMU/SWIOTLB 與完整 fault framework 尚未自動化。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

很多腳本看似穩定，其實用 `|| true` 吞掉 crash、清空全域 dmesg、或卸載不是本次載入的 module。這種 test pass 只表示腳本走到最後。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **stress** | 反覆/併行放大 timing 與 resource bug | 不覆蓋所有 interleaving |
| **fault injection** | 可控制地觸發 allocation/timeout/error/remove path | 不是任意讓系統壞掉 |
| **oracle** | 能客觀判定 pass/fail 的 invariant/evidence | 不只是最後一行文字 |
| **regression** | 修正後持續防止同類 bug 回歸的 test | 不等於一次手動重跑 |

## 心智模型

把 test 想成安全檢查機：不只要正常物件通過，也要故障樣本確實被攔下。若你不知道 test 在 bug 存在時是否會 fail，就沒有可信 oracle。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
define invariant and ownership
→ isolate run-specific logs/resources
→ repeat and parallel workload
→ inject one controlled fault
→ require expected error/cleanup
→ scan warnings/sanitizers
→ preserve reproducible bug diary
```

## 從簡單到精確

### Current source map

- `stress-03-parallel.sh`：Lab03 parallel userspace workload。
- `stress-03-reload.sh`：repeated module ownership/load/unload。
- `test.sh`：目前 scaffold 的入口與 expected exit handling。
- 各 Lab `test.sh`：應作為後續 fault cases 的最小 oracle。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```text
test records whether it loaded the module
→ only unloads what it owns
→ captures log position before run
→ validates only newly added lines
→ accepts only documented success/expected timeout codes
```

## 看似合理但錯誤的寫法

```sh
some_command || true
dmesg -C
rmmod module || true
echo passed
```
它會隱藏真正錯誤、破壞共享系統 log，並可能卸載別人的 state。

## 如何執行與觀察

```sh
cd labs/09-stress-and-fault-injection
./test.sh
STRESS_ITERATIONS=100 ./stress-03-reload.sh
STRESS_WORKERS=8 ./stress-03-parallel.sh
```

### 能證明／不能證明

可靠 evidence 需要 exact command、iteration/workers、kernel config、兩 repo SHA、run-specific stdout/stderr/dmesg 與 cleanup state。沒有 warning 不等於沒有 bug。

## Debug order

1. 先確認 test 是否真的能在已知 bug/fault 下 fail。
2. 確認 module/resource ownership，不碰別人的 loaded state。
3. 只分析本次新增 logs，處理 ring-buffer wrap。
4. 分開 expected timeout/interrupt 與 unexpected crash/I/O error。
5. 將最小 reproducer 固化為 regression。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `repeat/parallel` | 放大 timing/resource race | 所有 failure mode |
| `KASAN/KCSAN/lockdep` | memory/data-race/locking evidence | device protocol proof |
| `fault injection` | 指定 error path/recovery | real hardware 全部 fault |
| `bug diary` | 保存可重現推理與修法 | 自動測試本身 |

## 與 pcie-study 的對應

PCIe/accelerator 最危險的 bug 常出現在 timeout、reset、remove、late IRQ/DMA；Lab09 是把 Lab06/07 從 happy path 推向可面試、可維護 evidence 的入口。對應 `pcie-study` P2-18、P2-19、P3-10。

## 常見誤解

### 誤解：跑越久就一定找到 race

只增加機率；沒有 oracle/fault coverage 仍可能永遠 pass。

### 誤解：Sanitizer 沒報告就安全

它只覆蓋特定 instrumentation 與執行路徑。

### 誤解：可以先清 dmesg 方便測試

全域 log 是共享資源，應以位置/時間隔離本次訊息。

## 適用邊界與尚未驗證

- 目前主要 stress target 是 Lab03。
- 真實 IRQ/DMA fault 需要 QEMU/device-specific hooks 或 real hardware。
- Stress 結果受 scheduler、CPU count、kernel config 與 machine load 影響。

## 第一次閱讀先記住

1. Test 必須能在壞掉時可靠 fail。
2. 不清全域 log、不卸載非本次擁有的 module。
3. 把 timeout/reset/remove 變成 first-class test。

## Self-check

1. 為什麼 broad `|| true` 會破壞 oracle？
2. 測試為什麼要追蹤 module ownership？
3. 不清 dmesg 如何隔離本次 logs？
4. Stress 與 fault injection 各自增加什麼 evidence？
5. 下一個最有價值的 Lab07 fault case 是什麼？

<details>
<summary>參考答案</summary>

1. 它把 expected 與 unexpected failure 都轉成成功，test 無法區分 crash、I/O error 或正常 timeout。
2. 避免卸載測試開始前已由其他人/流程載入的 module，破壞共享 state。
3. 記錄 run 前位置/時間，只取新增 lines；若 ring buffer wrap，test 應明確失敗或改用 journal cursor。
4. Stress 放大 timing/interleaving；fault injection 確認指定 error/cleanup/recovery path。兩者皆非完整 proof。
5. 受控 IRQ/command timeout 後 reset 失敗，驗證 mapping 不被 free、無 DMA UAF，並保存 quiesce evidence。

</details>

## 來源與查證

- KASAN: <https://docs.kernel.org/dev-tools/kasan.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
- Fault injection: <https://docs.kernel.org/fault-injection/index.html>
- Current source: `labs/09-stress-and-fault-injection/`
