# 09 - Stress and Fault Injection

## 目標

把前面做出的 driver 從「能跑」提升到「能驗證」。

> [!NOTE]
> 這一關目前 repo 已有的是 `03-ioctl-poll-mmap` 專用 stress 腳本。
> `KUnit`、`kselftest`、`failslab`、`fail_page_alloc`、`fail_usercopy` 仍屬後續擴充題。

## 先備條件

- 你已經讀過 [`../../docs/onboarding/07-to-09-runtime-validation-bridge.md`](../../docs/onboarding/07-to-09-runtime-validation-bridge.md)
- 前面至少已經有一個真正可用的 driver lab
- 你知道正常路徑與 error path 是兩件不同的事

## 這一關要練什麼

- repeated load / unload
- parallel open / close
- long-running stress
- `failslab`
- `fail_page_alloc`
- `fail_usercopy`
- regression 紀律

## 成功標準

- 有 smoke / stress / regression 分層
- 有故障注入腳本
- 有 cleanup path 驗證

## 現在 repo 已有的東西

- `stress-03-reload.sh`
  - 針對 `03` 做 repeated load / unload
- `stress-03-parallel.sh`
  - 針對 `03` 做 parallel access
- `test.sh`
  - 先把上述兩支腳本串成最小 stress 套件

## Source 旁讀文件

讀 source 時可以直接打開同目錄的 companion doc，不需要回到 `docs/` 裡找對應解釋：

| Source | 旁讀文件 | 建議用途 |
|---|---|---|
| [`test.sh`](test.sh) | [`test.sh.md`](test.sh.md) | 理解 Lab09 目前如何串起 reload 與 parallel stress suite。 |
| [`stress-03-reload.sh`](stress-03-reload.sh) | [`stress-03-reload.sh.md`](stress-03-reload.sh.md) | 理解 repeated load/unload 如何驗 Lab03 cleanup 對稱性。 |
| [`stress-03-parallel.sh`](stress-03-parallel.sh) | [`stress-03-parallel.sh.md`](stress-03-parallel.sh.md) | 理解 parallel workers 如何透過 CLI/runtime 對 Lab03 施壓。 |
| [`Makefile`](Makefile) | [`Makefile.md`](Makefile.md) | 理解目前 scaffold Makefile 為什麼不直接跑 stress。 |
| [`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c) | [`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md) | 回頭理解 stress target driver 的 read/write/ioctl/poll/mmap path。 |

## 這一關會使用哪些 filesystem 入口

`09` 目前不是新的 driver。它主要反覆操作 `03` 的入口：

| 入口 | 第一輪用途 |
|---|---|
| `/dev/driver_lab_ctl0` | parallel stress workers 反覆做 `ioctl-write/status/read/trigger` 的 device node。 |
| `/sys/class/driver_lab_ctl/driver_lab_ctl0` | 可用來確認 repeated load/unload 後 device model entry 是否正常消失又重建。 |
| `dmesg` | 失敗時第一個看 kernel log，尤其是 cleanup、warning、oops。 |
| script stdout/stderr | 觀察 `stress-03-reload passed.`、`stress-03-parallel passed.`。 |

未來加入 failslab、fail_page_alloc、fail_usercopy 時，才會更大量使用 debugfs 裡的 fault injection controls。

## `test.sh` 與兩支 stress script 在做什麼

`test.sh` 目前只負責串接：

1. `stress-03-reload.sh`
2. `stress-03-parallel.sh`

`stress-03-reload.sh` 的重點：

- build `03` module
- 連續 load/unload 20 次
- 每次 load 後檢查 `/dev`、`/sys/class`、`/proc/devices`
- 每次 unload 後檢查 sysfs class device 已移除
- 如果 cleanup 不對稱，這類測試通常比單次 smoke test 更容易暴露問題

`stress-03-parallel.sh` 的重點：

- build `03` module 與 runtime CLI
- 載入 `03` module
- 先確認 filesystem surface 已出現，再啟動 parallel workers
- 啟動 4 個 worker 反覆做 `ioctl-write`、`status`、`read`、`trigger`
- 提高 read/write/ioctl/poll 共享狀態被同時碰到的機率

這些不是完整 fault injection。它們是第一批可重複的壓力驗證習慣。

## 現在 repo 還沒有的東西

- `KUnit` 測試
- `kselftest` 整合
- `failslab` / `fail_page_alloc` / `fail_usercopy` 自動化
- `05-07` 專用 stress / regression matrix

## 新手先記住這一關在補什麼

- 「能跑一次」不等於「driver 寫對了」
- 真正難的是 repeated load/unload、失敗注入、cleanup 對稱性

## 目前已完成的部分

- 針對 `03-ioctl-poll-mmap` 的 repeated load/unload 壓力腳本
- 針對 `03-ioctl-poll-mmap` 的 parallel access 壓力腳本

## 目前還沒完成的部分

- `failslab` / `fail_page_alloc` / `fail_usercopy` 的自動化腳本
- 更長時間的 regression matrix
- 針對 QEMU EDU labs 的專用 stress 套件

## 這一關現在怎麼看待

這一關現在比較像：

- 已經有第一批可執行的驗證習慣
- 但還沒有進到完整 fault injection 與 regression framework

如果你目前是新手，這樣的成熟度是合理的。
重點不是假裝全部都完成，而是清楚知道「已經有什麼」、「下一步還缺什麼」。

## 新手第一次不要追的東西

- 一開始不要追非常長時間的 soak test
- 先把「可以穩定重複 20 次」做好
- 先把 repeated load/unload 與 parallel access 練熟

## 第一次理想上要做到的最小版本

第一次只要求你做到：

1. repeated load/unload 可以連跑 20 次
2. parallel access 可以穩定重現與觀察結果
3. 每次失敗時知道要回頭看哪一份 log

## 如果你完全看不懂 scripts，先看這 5 個點

1. `stress-03-reload.sh` 的 `while [ "$i" -lt 20 ]`：重複 load/unload。
2. `stress-03-reload.sh` 的 `cleanup()`：失敗或中斷時仍嘗試卸載 module。
3. `stress-03-parallel.sh` 的 `worker()`：每個 worker 都反覆打同一個 driver。
4. `timeout 2s ... read || true`：parallel read 可能被別的 worker 先消費資料，所以避免永久卡住。
5. `test.sh`：目前只是串接兩個 `03` stress scripts，不是 fault injection framework。

## script 參數第一輪怎麼讀

`09` 主要是 shell stress scripts，不是新的 kernel API。這一關仍然沿用「參數角色」方法：看清楚哪些值控制次數、平行度、timeout、cleanup。

完整模板見 [`../../docs/onboarding/kernel-api-parameter-roles.md`](../../docs/onboarding/kernel-api-parameter-roles.md)。

| 位置 | 參數角色 | 第一輪理解 |
|---|---|---|
| `stress-03-reload.sh` 的迴圈次數 | stress count | 控制 load/unload 重複幾次，用來驗 cleanup 對稱性。 |
| `stress-03-parallel.sh` 的 worker 數 | parallelism | 控制同時有幾條 userspace 路徑打同一個 driver。 |
| `timeout 2s` | timeout guard | 避免某條 userspace 操作永久卡住，讓測試能失敗收斂。 |
| `cleanup()` | cleanup hook | script 中斷或失敗時仍嘗試卸載 module、清 build artifact。 |
| CLI device path | resource input | 指向要施壓的 `/dev/driver_lab_ctl0`。 |

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | repeated load/unload 主要驗 cleanup 對稱性；parallel access 主要驗共享狀態壓力；目前已有 `03` 專用 stress 腳本。 |
| 可以先略過 | KUnit、kselftest、`failslab`、`fail_page_alloc`、`fail_usercopy` 的完整框架與設定細節。 |
| 之後再回來補 | fault injection 自動化、長時間 regression matrix、`05-07` QEMU EDU stress suite。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這一關目前完成到哪裡？ | 目前已有 `03-ioctl-poll-mmap` 專用 repeated reload 與 parallel access stress 腳本。 |
| 這一關目前還沒完成什麼？ | 尚未完成 KUnit、kselftest、`failslab`、`fail_page_alloc`、`fail_usercopy` 自動化與完整 fault injection framework。 |
| repeated load/unload 主要驗什麼？ | 驗 init/exit cleanup 是否對稱，避免多跑幾次才暴露的 resource 泄漏或狀態殘留。 |
| parallel access 主要驗什麼？ | 對共享狀態施壓，讓 read/write/ioctl/poll 同時被碰到時的問題更容易出現。 |
| stress 和 regression 差在哪裡？ | stress 是重複施壓；regression 是每次修改後固定重跑，避免舊功能壞掉。 |
| 第一個觀測點是什麼？ | `stress-03-reload passed.`、`stress-03-parallel passed.`，以及失敗時的 `dmesg`。 |
| 失敗時第一個看哪裡？ | repeated reload 先看 cleanup path；parallel access 先看是否有 process 還持有 fd，再看 `dmesg`。 |

## 第一次卡住先看哪裡

- repeated load/unload 偶發失敗
  - 優先懷疑 cleanup path 不對稱
- parallel 測試結果不穩
  - 先分清楚這是教學上刻意示範 race，還是 module 本身壞掉
- 不知道 stress 跟 regression 差在哪
  - stress 是「重複施壓」
  - regression 是「每次修改後都應該固定重跑的檢查」
