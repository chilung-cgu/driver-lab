# 04 — Lost update、mutex、kthread 與停止同步

> **定位**：Lab04 把 `counter++` 拆成 read/modify/write，讓你觀察 lost update，再以 mutex 對照。另一半重點是 lifecycle：shared state 必須在 kthread 啟動前初始化，exit 用 `kthread_stop()` 等 worker 真正返回後才能釋放資源。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab04 把 `counter++` 拆成 read/modify/write，讓你觀察 lost update，再以 mutex 對照。另一半重點是 lifecycle：shared state 必須在 kthread 啟動前初始化，exit 用 `kthread_stop()` 等 worker 真正返回後才能釋放資源。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current source/CLI/test 可重現 unsafe/safe 差異並檢查 startup/stop；race demo 具機率性，仍需 repeated stress、KCSAN/lockdep 才能增加 evidence。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

只看到最終 counter 小於 expected，容易把問題簡化成『加 mutex 就好』。但如果 init 在 worker 啟動後清 state，或 exit 未等待 worker，仍會有 lost work/UAF。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **lost update** | 兩個 RMW 使用同一舊值而遺失其中一次更新 | 不等於 CPU 不會做 arithmetic |
| **critical section** | 必須一起維持 invariant 的 code/data 範圍 | 不一定等於整個 ioctl |
| **kthread** | 由 Linux scheduler 執行的 kernel task | 不是 hard IRQ |
| **stop synchronization** | 要求停止並等待 worker 退出 | 不只是寫一個 flag |

## 心智模型

把 counter 想成共享帳本：兩人同時讀 10、各自算 11、最後都寫 11。Mutex 讓整個 RMW 只能一人進行；kthread_stop 則像關門後等最後一位員工離開再拆店。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
initialize shared state
→ create device/UAPI resources
→ start kthread last
→ unsafe or mutex-protected RMW
→ reject new work
→ kthread_stop waits for return
→ free state/resources
```

## 從簡單到精確

### Current source map

- `driver_lab_race.c`：unsafe/safe increment、kthread function、init/exit。
- `driver_lab_race_uapi.h`：status/control ioctls。
- `tests/driver_lab_race_cli.c`：parallel workers 與 expected/observed。
- `test.sh`：startup, unsafe/safe, unload/reload gates。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```c
mutex_lock(&counter_lock);
counter++;
mutex_unlock(&counter_lock);
```
正確範圍是整個 read-modify-write；只在 read 或 write 一側加 lock 仍可能 lost update。

## 看似合理但錯誤的寫法

錯誤做法：`kthread_run()` 後再把 counter/stop state 初始化，或 exit 只設 stop flag 就立即 destroy device/free memory。Worker 可能已更新被清零，或仍在使用 freed state。

## 如何執行與觀察

```sh
cd labs/04-locking-and-races
./test.sh
```

CLI 可切 safe mode、reset、單次 increment 與多 thread race；同一組 workload 要比較 expected、observed 與 worker lifecycle。

### 能證明／不能證明

Unsafe path 的 deficit 與 safe path 的 expected count 是教學 evidence；一次沒出現 deficit 不能證明無 data race。卸載無 warning/late worker 也只覆蓋本次 timing。

## Debug order

1. 先畫所有 execution paths：ioctl callers、kthread、module exit。
2. 把 counter++ 展開成 load/add/store，找共享 invariant。
3. 確認 shared state 在 worker start 前完成初始化。
4. 確認 exit 使用 kthread_stop 等待返回。
5. 使用 repeated stress、KCSAN/lockdep 補機率性 demo。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `mutex` | RMW mutual exclusion | IRQ/device completion |
| `READ_ONCE/WRITE_ONCE` | 特定 access compiler/single-access discipline | multi-step invariant |
| `kthread_stop` | stop request + join-like wait | 其他 timer/IRQ/DMA producer |
| `KCSAN/lockdep` | data-race/locking evidence | 邏輯 race 完整 proof |

## 與 pcie-study 的對應

Queue producer、IRQ handler、timeout、remove 也會共享 state；Lab04 的重點是先建立 concurrency/lifetime map，再進 Lab05～07。對應 `pcie-study` P1-08、P1-09、P3-05。

## 常見誤解

### 誤解：READ_ONCE 能修 counter++

它不是 atomic RMW 或 lock。

### 誤解：測一次結果正確就沒有 race

不同 interleaving 可能尚未發生。

### 誤解：設 stop flag 就能 free

必須等待 worker/callback 完全退出。

## 適用邊界與尚未驗證

- 本 lab 是單一 teaching counter，不涵蓋 multi-field state machine。
- Mutex 適用 task context；hard IRQ 共享 state 需不同 lock/context design。
- PREEMPT_RT 與 target scheduling 會改變 timing，但不改變需明確 synchronization 的事實。

## 第一次閱讀先記住

1. Counter++ 是 RMW，不是單一不可分割動作。
2. 初始化要在 producer 啟動前。
3. 釋放前要 stop 並同步 in-flight worker。

## Self-check

1. Lost update 如何由兩個 counter++ 交錯產生？
2. 為什麼 READ_ONCE/WRITE_ONCE 不能修正它？
3. 為什麼 state 要在 kthread_run 前初始化？
4. kthread_stop 比只設 flag 多做什麼？
5. Safe path 一次通過能證明無 race 嗎？

<details>
<summary>參考答案</summary>

1. 兩邊先讀同一舊值，各自加一，再寫回同一結果，因此少一次更新。
2. 它們不把整個 read-modify-write 變原子，也不排除另一 writer 插入。
3. Worker 可能一啟動就讀寫 state；後初始化會清掉更新或看到未準備資料。
4. 它發出 stop request 並等待 thread function 返回，建立 free 前的 lifetime boundary。
5. 不能；它只提供該次 workload/timing evidence，仍需 repeated stress與 sanitizer。

</details>

## 來源與查證

- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- Kthread APIs: <https://docs.kernel.org/driver-api/basics.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
- Current source: `labs/04-locking-and-races/driver_lab_race.c`
