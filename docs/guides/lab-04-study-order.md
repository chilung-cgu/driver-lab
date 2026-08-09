# Lab04 讀懂順序

> 這份文件是 `04-locking-and-races` 的閱讀入口。它安排順序，不取代 current source、Lab README 或官方 locking 文件。

## 進入 Lab04 前

先確認你能回答：

- Lab03 的 `read/write/ioctl/poll/mmap` 為什麼可能同時碰同一份 state？
- syscall callback、background kthread 與 IRQ handler 都可能成為獨立 execution path，差別在哪裡？
- cleanup 為什麼必須先停仍會執行的 producer/worker，再拆它可能使用的資源？

不穩時先讀：

1. [`../onboarding/01-to-03-user-kernel-abi-bridge.md`](../onboarding/START-HERE.md)
2. [`../../labs/03-ioctl-poll-mmap/README.md`](../../labs/03-ioctl-poll-mmap/README.md)
3. [`../concepts/concurrency-primer.md`](../concepts/concurrency-primer.md)
4. [`../onboarding/kernel-api-parameter-roles.md`](../onboarding/kernel-interfaces.md)

## 建議閱讀順序

1. [`../onboarding/03-to-05-concurrency-pci-bridge.md`](../onboarding/START-HERE.md) 的 `03 → 04`。
2. [`../concepts/concurrency-primer.md`](../concepts/concurrency-primer.md)：先分清 mutex、spinlock、waitqueue、completion 解決的問題不同。
3. [`lab-04-walkthrough.md`](lab-04-study-order.md)：建立 lost update 與 unsafe/safe 對照。
4. [`../../labs/04-locking-and-races/README.md`](../../labs/04-locking-and-races/README.md)：看本 lab 的邊界與驗收。
5. [`../../labs/04-locking-and-races/driver_lab_race_uapi.h`](../../labs/04-locking-and-races/driver_lab_race_uapi.h)：先讀 ABI。
6. [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c)：看 userspace 如何產生並行 ioctl。
7. [`../../labs/04-locking-and-races/driver_lab_race.c`](../../labs/04-locking-and-races/driver_lab_race.c)：照下方 source trace。
8. [`../../labs/04-locking-and-races/test.sh`](../../labs/04-locking-and-races/test.sh)：理解 smoke test 能證明與不能證明的事。
9. [`../../labs/04-locking-and-races/debug-checklist.md`](../../labs/04-locking-and-races/debug-checklist.md) 與 [`../reference/debugging-playbook.md`](../reference/debugging.md)。

Companion `.c.md/.h.md/.sh.md` 若與 current source 或 accuracy audit 不一致，以 current source為準。

## 三輪閱讀法

### 第一輪：先找出誰和誰競爭

標出：

- `dl_counter`：刻意示範 lost update 的共享資料；
- userspace ioctl threads：外部並行來源；
- `dl_race_worker_fn()`：driver 內部並行來源；
- `dl_safe_mode`：決定走故意錯誤或 mutex 路徑。

Unsafe path：

```text
read counter
→ usleep_range() 放大競爭視窗
→ write snapshot + 1
```

兩條 execution paths 可能都讀到同一舊值，最後覆蓋彼此更新。

### 第二輪：看 mutex 保護哪個 invariant

Safe path用 `dl_race_lock` 包住完整 read-modify-write，使另一條路徑必須在前一條更新完成後才讀取。

注意：

- `READ_ONCE(dl_safe_mode)` 只控制一次存取，不是 mutex，也不會讓 `dl_counter++` 原子化；
- mutex 可睡，只能用在允許睡眠的 context；
- 真正 IRQ-shared state 後續可能需要 spinlock 或不同設計，不能把本 lab 的 mutex 無條件搬到 hard IRQ。

### 第三輪：看 startup 與 teardown lifetime

Current corrected source先初始化 worker會碰的state，再呼叫`kthread_run()`，避免thread已更新counter後又被init路徑清零。

Exit路徑：

```text
kthread_stop()
→ 確認worker function退出
→ 更新state
→ destroy device/class/cdev/devt
```

重點不是背反序，而是：**先停止與同步仍可能執行的使用者，再釋放它可能觸及的資源。**

## Source trace

依序看：

1. `dl_counter`、`dl_safe_mode`、`dl_worker_running`。
2. `dl_race_increment_unlocked()`。
3. `dl_race_increment_locked()`。
4. `dl_race_increment()`。
5. `dl_race_worker_fn()`。
6. `dl_race_ioctl()`。
7. `driver_lab_race_init()`：特別看 initialize-before-start。
8. `driver_lab_race_exit()`：特別看 `kthread_stop()`。

## 實驗

```sh
cd labs/04-locking-and-races
./test.sh
```

手動觀察：

```sh
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50

../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
```

### Smoke test 的證據邊界

Current test檢查safe run的observed count不小於unsafe run。這是教學型probabilistic gate，不是形式化data-race證明：

- unsafe不一定每次都顯示明顯lost update；
- safe count還會包含background worker；
- scheduler interleaving每次不同；
- KCSAN、lockdep與長時間stress回答的是不同問題。

因此不要把「跑十次沒失敗」當成沒有race的證明。

## 第一輪可以先略過

- PREEMPT_RT下lock implementation差異；
- lockdep/KCSAN/KASAN設定細節；
- lock-free algorithm與完整Linux memory model；
- PCI IRQ的per-vector locking；
- priority inversion與real-time mutex。

## Self-check

1. Unsafe mode的lost update是如何發生的？
2. `READ_ONCE()`為什麼不能替代mutex？
3. Current init為什麼要先初始化state再啟動kthread？
4. `kthread_stop()`比只設一個bool更重要在哪裡？
5. Smoke test為什麼不能證明unsafe code沒有race？

<details>
<summary>參考答案</summary>

1. 多條路徑都先讀到同一舊counter，各自算`old+1`後寫回相同新值，造成其中一次更新消失。
2. `READ_ONCE()`只限制該次compiler-visible access；它不讓跨多條指令的read-modify-write互斥或原子，也不建立完整critical section。
3. Thread一旦啟動即可立即執行；若之後才清counter或設定mode，init會覆蓋worker已產生的更新，形成非教學目標的startup race。
4. `kthread_stop()`設定stop condition並等待thread function返回，提供退出同步；只設bool後立刻free資源，worker可能尚未觀察到bool並仍在執行。
5. Race取決於interleaving，有限次測試可能剛好未觸發；測試只能增加暴露機率，仍需設計推理、KCSAN與壓力驗證。

</details>

## 官方查證入口

- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- Mutex design: <https://docs.kernel.org/locking/mutex-design.html>
- Driver basics / kthreads: <https://docs.kernel.org/driver-api/basics.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
- Testing guide: <https://docs.kernel.org/dev-tools/testing-overview.html>
