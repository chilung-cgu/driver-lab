# 01 — debugfs、seq_file 與可控制的 logging

> **定位**：Lab01 建立 driver 可觀測性：用 debugfs 導出狀態與測試入口，用 `pr_info()`/`pr_debug()` 與 dynamic debug 控制 log。Debugfs 是開發介面，不是穩定產品 UAPI；helper 與 callback 共用 state 時仍需相同 synchronization。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab01 建立 driver 可觀測性：用 debugfs 導出狀態與測試入口，用 `pr_info()`/`pr_debug()` 與 dynamic debug 控制 log。Debugfs 是開發介面，不是穩定產品 UAPI；helper 與 callback 共用 state 時仍需相同 synchronization。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current source 使用 atomic scalar knobs 與 mutex-protected message；smoke test 驗 entry、trigger、status、dynamic-debug optional path 與 cleanup。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

只有大量 printk 很難知道 driver 現在的 state，也會擾動 timing。但單純建立 debugfs 檔案也不夠：讀寫 callback、helper 與 unload 必須共享正確 lifetime/synchronization。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **debugfs** | kernel 開發與除錯用 filesystem surface | 不承諾 stable UAPI |
| **seq_file** | 安全產生可分段讀取文字輸出的 helper | 不自動鎖共享 state |
| **dynamic debug** | runtime 選擇性開啟 pr_debug/dev_dbg callsite | 不等於 device tracing protocol |
| **dentry** | debugfs 目錄/檔案在 VFS 中的 object | 不是普通 userspace fd |

## 心智模型

把 `trigger` 想成測試按鈕，`status` 是儀表板，dynamic debug 是可單獨打開的詳細記錄。按鈕、儀表與內部 state 必須使用同一套 synchronization，卸載時先移除入口再釋放 state。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
module init
→ 建立 debugfs directory/files
→ userspace read/write 進 file_operations callbacks
→ 同步更新 counter/message
→ status/log 可觀察
→ remove_recursive 後才結束 module lifetime
```

## 從簡單到精確

### Current source map

- `driver_lab_debugfs_logging.c`：`dl_status_show()`、`dl_trigger_write()`、debugfs init/exit。
- `test.sh`：mount/debugfs、entry existence、trigger/state/log 與 unload。
- `scripts/mount-debugfs.sh`：只處理 debugfs mount prerequisite。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```text
write trigger
→ callback validates/copies input
→ update shared state under its synchronization
→ read status obtains a consistent snapshot
→ unload removes entries before backing state disappears
```

## 看似合理但錯誤的寫法

錯誤做法是讓 `debugfs_create_atomic_t()` 操作一個 scalar，但另一條 callback 用 plain read/write 修改同一變數；兩條路徑沒有共享同一 synchronization contract。

## 如何執行與觀察

```sh
cd labs/01-debugfs-logging
./test.sh
```

手動路徑：載入後讀 `status`，寫 `trigger`，再讀 `trigger_count`；若 `/proc/dynamic_debug/control` 存在，再只開啟本 module 的 `pr_debug()`。

### 能證明／不能證明

Entry 存在、trigger 後 state 改變、log 出現、unload 後 directory 消失，可證明最小 observation path。不能證明 debugfs ABI 穩定、高併發正確或 logging 對性能無影響。

## Debug order

1. 確認 debugfs 已掛載且 kernel config 支援。
2. 確認 module init 成功及 root dentry 非 error pointer。
3. 分開查 entry permission、callback return 與 state synchronization。
4. Dynamic debug 缺席時視為 optional capability，不先判 lab 失敗。
5. 卸載後仍有 entry 時，優先查 cleanup/lifetime。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `debugfs` | 導出 debug state/knob | 穩定產品 ABI |
| `seq_file` | 格式化文字讀取 | 共享 state 互斥 |
| `dynamic debug` | 選擇性啟用 debug callsite | 結構化 event/payload correctness |
| `tracepoints/ftrace` | 低侵入時序觀察 | 自動修正 race |

## 與 pcie-study 的對應

PCIe bring-up 會需要可讀的 BAR/IRQ/DMA state 與動態 log；這一關先學會『先設計 observation，再 debug』。對應 `pcie-study` P3-02、P2-19。

## 常見誤解

### 誤解：debugfs 是正式 ABI

它可隨 kernel/driver 改變，產品 UAPI 應使用穩定介面。

### 誤解：seq_file 會自動同步

它處理輸出流程，不知道你的 shared-state invariant。

### 誤解：log 越多越容易除錯

高頻 log 會淹沒訊息、改變 timing 並增加 latency。

## 適用邊界與尚未驗證

- Debugfs/dynamic debug 可被 config 或 permission 關閉。
- 本 lab 只處理很小 state，未測高頻多 reader/writer。
- 產品 driver 常需 tracepoints、devlink/debugfs policy 或 subsystem-specific telemetry。

## 第一次閱讀先記住

1. Debug surface 也有 resource、concurrency 與 cleanup。
2. 可觀測性不是無限制 printk。
3. 同一 state 的所有 access path 必須共享一致 synchronization。

## Self-check

1. Debugfs 為什麼不應當 stable UAPI？
2. seq_file 解決什麼，沒有解決什麼？
3. 寫 `trigger` 後如何證明 callback 真正執行？
4. Dynamic debug control 不存在時該如何判斷？
5. 卸載時為什麼要先移除 entries？

<details>
<summary>參考答案</summary>

1. 它是 kernel debug interface，layout/semantics 可隨版本與 driver 改變，沒有產品 ABI 相容承諾。
2. 它協助產生可分段讀取的文字；不提供 shared state lock、lifetime 或 snapshot consistency。
3. 同時觀察 callback-associated counter/message、run-specific kernel log 與 expected return，而不是只看 write command 成功。
4. 把它視為 kernel optional feature；仍驗基本 debugfs path，不虛構 pr_debug evidence。
5. 避免新 open/read/write 進入，並防止 VFS callback 使用已釋放 backing state。

</details>

## 來源與查證

- Debugfs: <https://docs.kernel.org/filesystems/debugfs.html>
- Seq_file: <https://docs.kernel.org/filesystems/seq_file.html>
- Dynamic debug: <https://docs.kernel.org/admin-guide/dynamic-debug-howto.html>
- Current source: `labs/01-debugfs-logging/driver_lab_debugfs_logging.c`
