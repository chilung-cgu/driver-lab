# Concurrency primer — shared state、ordering、waiting 與 lifetime

> **定位**：進 Lab04 前的唯一 concurrency 前導；也作為 Labs06/07 IRQ/DMA teardown 的共同心智模型。

## 先講結論

Concurrency 問題不只是在 counter 周圍加 lock。你要先分清五層：

```text
mutual exclusion  ：誰能同時修改 shared invariant
atomicity         ：單次更新能否被拆開/插入
memory ordering   ：不同 access 對 observer 可見的先後
waiting/completion：誰在等哪個 predicate/event
lifetime/quiesce  ：resource free 前誰仍可能使用
```

同一份 driver state 可能同時被 syscall callbacks、kthread/workqueue、hard/threaded IRQ、timer、remove/reset 與 device DMA 使用。只列 userspace threads 會漏掉真正的 execution paths。

## 不確定處與驗證狀態

- Lock/context implementation 會受 PREEMPT_RT、architecture 與 kernel config 影響。
- KCSAN/lockdep/stress 增加 evidence，不是形式化證明。
- Device ownership/ordering 還需 DMA/MMIO/device protocol；CPU lock 不能取代。

## 名詞先說清楚

| 名詞 | 意思 | 不代表什麼 |
|---|---|---|
| execution path | 可能獨立交錯執行的 callback/thread/IRQ/device action | 只指 process/thread |
| shared state | 多 path 可讀寫或其 lifetime 相互依賴的 object | 只指 global variable |
| data race | 未同步的 concurrent conflicting access 到同一 location | 所有 logical race |
| race condition | 結果依事件順序而可能破壞邏輯 | 必然是 C data race |
| invariant | 多個欄位/步驟必須共同成立的條件 | 單一變數值 |
| in-flight | 已開始、尚未證明退出/完成的使用者或工作 | CPU 正在某行 code 而已 |
| quiesce | 停新 producer 並同步既有使用者 | 只寫 stop flag |

## 心智模型：共享帳本與關店

`counter++` 實際是 read → add → write。兩人同讀舊值會 lost update。Mutex 像鎖住整段帳本修改。

Teardown 則像關店：先停止接新單、停 worker/device producer、等店內人離開，最後才拆櫃台和倉庫。只把 create API 倒著呼叫，無法處理晚到 IRQ/DMA/VMA。

## 四步 concurrency map

1. **列 state**：counter、queue indices、status、mapping、pointer、refcount。
2. **列 paths**：read/write/ioctl、worker、IRQ、timeout、remove、DMA。
3. **列 contract**：mutex/atomic/release-acquire/wait predicate/ownership。
4. **列 lifetime**：誰 publish、誰 stop、誰 join/synchronize、何時 free。

## 工具分工

| 工具 | 適合 | 不解決 |
|---|---|---|
| mutex | 可睡 task context 的 multi-step invariant | hard IRQ、device completion |
| spinlock | 短 atomic/IRQ-shared critical section | sleep/usercopy/long work |
| atomic RMW | 單一 counter/bit/reference update | multi-field invariant |
| READ_ONCE/WRITE_ONCE | marked single access/compiler discipline | lock、general publication |
| acquire/release/barrier | memory visibility/order | mutual exclusion、wait hardware |
| wait queue | 等 predicate，wake 後 recheck | 保存 payload、保證 ready |
| completion | 一次性/計數完成通知 | device source ACK、persistent queue |
| kthread_stop | stop request + wait thread return | IRQ/timer/DMA 等其他 producer |
| synchronize_irq | 等 in-flight handler 退出 | 停 device 產生新 IRQ |
| refcount/kref | object users 的 lifetime accounting | protocol ordering |

## 正確範式與反例

### Lost update

```c
mutex_lock(&lock);
counter++;
mutex_unlock(&lock);
```

Lock 必須包住完整 RMW。只在 read 或 write 一側上鎖仍可能 lost update。

### Publication

```c
data = value;
smp_store_release(&ready, 1);

if (smp_load_acquire(&ready))
    use(data);
```

`WRITE_ONCE(ready, 1)` 單獨不建立一般 CPU publication contract。

### Wait predicate

```text
while predicate is false:
    sleep on wait queue
wake → acquire protection → recheck predicate
```

兩 reader 被同一 event 喚醒後，第一個可能已消費 record；第二個必須 recheck。

### Teardown

```text
unpublish/reject new work
→ stop/mask producer
→ wait/join/synchronize in-flight paths
→ free resources in dependency order
```

錯誤範例是只設 bool 後立即 free，或 unmap BAR/free DMA buffer 後才關 IRQ/device。

## Lab 對應

- Lab03：mutex 只保護 kernel writers；mmap snapshot 另用 sequence publication。
- Lab04：lost update、mutex、initialize-before-kthread、`kthread_stop()`。
- Lab06：ACK/mask source，`synchronize_irq()` 後再 free state/MMIO。
- Lab07：completion/idle、DMA ordering、quiesce-before-free。
- Lab09：parallel/reload、KCSAN/lockdep/fault injection roadmap。

## Debug 問法

- 哪兩條 path 可同時執行？
- 它們是否碰同一 location，或碰不同欄位卻共享 invariant？
- Lock 是否在所有 access paths 使用同一 contract？
- Wake 的 predicate 是否在保護下 recheck？
- Timeout/remove 後是否可能有 late completion？
- Object free 前是否同步所有 fd/VMA/work/IRQ/DMA users？

## Self-check

1. Data race 與 race condition 差在哪？
2. `atomic_inc()` 為什麼不一定保護 multi-field state？
3. Wakeup 後為什麼還要 recheck predicate？
4. `synchronize_irq()` 為什麼不能取代 device mask/ACK？
5. Barrier、lock、completion、quiesce 各解哪一層？

<details>
<summary>參考答案</summary>

1. Data race 是同一 location 的未同步 conflicting access；race condition 更廣，atomic operations 的事件順序也可能破壞邏輯。
2. 它只讓單一 RMW 原子；counter、list、owner、state 之間的 invariant 仍可能被其他 path 看到不一致。
3. Wake 只表示狀態可能改變；其他 reader/consumer 可在你取得保護前先消費或清除條件。
4. 它只等待已進 handler 的執行者退出；未 mask/ACK 的 device 可繼續產生新 event。
5. Lock 解互斥，barrier 解可見順序，completion 解已發生事件的等待，quiesce 解 free 前停止/同步所有 producers/users。

</details>

## 來源與查證

- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- Mutex design: <https://docs.kernel.org/locking/mutex-design.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- Wait/completion/kthread basics: <https://docs.kernel.org/driver-api/basics.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
