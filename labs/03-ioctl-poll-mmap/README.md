# 03 — ioctl、poll、blocking read 與 read-only mmap snapshot

> **定位**：Lab03 在同一 char device 中分開 data、control、event 與 mapping 四條路徑。Blocking/poll wakeup 只要求重新檢查 predicate；多 reader 取得 mutex 後仍要 recheck。Kernel mutex 無法保護 arbitrary userspace mmap load，因此 snapshot 使用 read-only odd/even sequence publication。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab03 在同一 char device 中分開 data、control、event 與 mapping 四條路徑。Blocking/poll wakeup 只要求重新檢查 predicate；多 reader 取得 mutex 後仍要 recheck。Kernel mutex 無法保護 arbitrary userspace mmap load，因此 snapshot 使用 read-only odd/even sequence publication。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current source/test 已修正 multi-reader、actual PAGE_SIZE、read-only VMA、mprotect rejection、sequence snapshot 與 fixed-width UAPI；仍需 target runtime/KCSAN 與 VMA lifetime stress。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

把 wakeup 當成『條件一定成立』會讓第二個 reader 錯誤返回；把 kernel mutex 當成 userspace mmap reader 也會取得的鎖，則會接受 torn snapshot。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **predicate** | wait/poll 每次醒來重新判斷的 readiness 條件 | wake 本身不等於 ready |
| **wait queue** | 讓 task sleep 並在 event 後重新排程的 mechanism | 不儲存 payload |
| **VMA** | 一段 userspace virtual memory mapping 的 kernel object | 不等於 backing page owner |
| **sequence publication** | odd=更新中、even=穩定的 snapshot protocol | 不提供 writer mutual exclusion |

## 心智模型

把四條 path 想成同一設備的資料窗口、控制表單、門鈴與唯讀儀表板。門鈴只叫你回來看狀態；儀表板讀者用 sequence 確認前後是同一個完整畫面。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
writer copies input
→ mutex protects kernel state
→ seq odd
→ update snapshot fields
→ seq even
→ wake waiters
→ reader/poll rechecks predicate; mmap reader retries on odd/change
```

## 從簡單到精確

### Current source map

- `driver_lab_ioctl_poll_mmap.c`：read/write/ioctl/poll/mmap 與 `dl_sync_shared_page_locked()`。
- `runtime/include/driver_lab_uapi.h`：fixed-width UAPI/snapshot layout。
- `runtime/src/driver_lab_runtime.c`：userspace atomic snapshot reader。
- `test.sh`：two-reader、empty poll、read-only mmap/mprotect 與 cleanup regressions。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```c
WRITE_ONCE(shared->seq, odd);
smp_wmb();              /* odd visible before fields */
update_fields();
smp_wmb();              /* fields visible before even */
WRITE_ONCE(shared->seq, even);
```
Userspace 先讀 even seq、複製 snapshot、再讀 seq；前後不同或 odd 就 retry。

## 看似合理但錯誤的寫法

錯誤做法：wait_event 醒來後不在 mutex 下 recheck global record，或 mmap 成 writable，讓 userspace 可破壞 kernel-published metadata。

## 如何執行與觀察

```sh
cd labs/03-ioctl-poll-mmap
./test.sh
```

測試會同時使用 kernel module、runtime/CLI 與多 process helpers；注意它驗的是本次 run 的 predicates、permissions、snapshot 與 cleanup。

### 能證明／不能證明

Two-reader regression、poll timeout/readiness、mmap read-only、mprotect rejection、sequence snapshot 與卸載可提供具體 evidence。仍不能證明所有 weak-memory interleaving 或 malicious UAPI input。

## Debug order

1. 先分清是 read predicate、poll mask、ioctl state 還是 mmap snapshot 問題。
2. 確認 wakeup 前 state 已在 mutex 下發布。
3. 多 reader 問題要看拿鎖後的第二次 predicate check。
4. mmap 問題要看 PAGE_SIZE、VMA flags、vm_insert_page 與 mapping lifetime。
5. snapshot 問題保存 begin/end seq 與接受/重試條件。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `mutex` | kernel writers/readers 的 shared-state invariant | userspace mmap loads |
| `wait queue/wake` | sleep/wakeup 與重新檢查 | 條件一定成立 |
| `smp_wmb + sequence` | snapshot publication order/detection | writer mutual exclusion |
| `VMA flags` | mapping permission/lifetime policy | device DMA mapping |

## 與 pcie-study 的對應

這四條 path 會演進為 accelerator 的 data/control/event/mapping UAPI；但 PCIe 還加入 MMIO、IRQ、DMA ownership、IOMMU 與 hot-remove。對應 `pcie-study` P1-10、P1-14、P2-20、P3-04。

## 常見誤解

### 誤解：wake 會讓 poll 成功回 revents=0

wake 只促使重新評估；條件仍 false 時通常繼續睡。

### 誤解：mutex 可保護 mmap reader

任意 userspace load 不會取得 kernel mutex。

### 誤解：頁面一定 4096 bytes

PAGE_SIZE 依 architecture/kernel build。

## 適用邊界與尚未驗證

- Sequence protocol 只提供 snapshot consistency detection，不是 general shared-memory transaction。
- 本 lab 沒有 hot-unplug、multiple devices、VMA close refcount 或 pinned user DMA。
- Weak-memory correctness 仍應在 target architecture/KCSAN/stress 下驗證。

## 第一次閱讀先記住

1. Wakeup 與 predicate 是兩件事。
2. 拿到 mutex 後仍要 recheck shared condition。
3. Mmap reader 需要 publication protocol與 permission/lifetime。

## Self-check

1. 為什麼兩個 blocking reader 被同一 write 喚醒後，第二個仍要 recheck？
2. Wakeup 能證明 poll 的什麼？
3. Kernel mutex 為什麼不能讓 userspace mmap snapshot 一致？
4. Odd/even sequence reader 的接受條件是什麼？
5. Read-only VMA 為什麼還要清 VM_MAYWRITE？

<details>
<summary>參考答案</summary>

1. 第一個 reader 可能先取得 mutex 並消費 global record；第二個拿鎖時 predicate 已改變，必須繼續等或回 EAGAIN。
2. 只證明 waiter 被要求重新排程/評估；不保證 readiness、成功 return 或 payload。
3. Userspace load 不參與 kernel lock protocol，可能在 writer 更新中間讀到欄位組合；需要 sequence publication/retry。
4. 開始 seq 為 even，複製後 end 與 begin 相同且仍 even，snapshot 內 seq 也一致。
5. 避免 userspace 之後用 mprotect 把 mapping 升級成 writable，破壞 kernel-owned page。

</details>

## 來源與查證

- Poll/wait queues: <https://docs.kernel.org/driver-api/basics.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- Memory mapping APIs: <https://docs.kernel.org/core-api/mm-api.html>
- Current source: `labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c`
