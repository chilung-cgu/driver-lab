# Lab04 讀懂順序

這份文件是 `04-locking-and-races` 的第一入口。它不取代
[`lab-04-walkthrough.md`](lab-04-walkthrough.md) 或 source companion docs，而是把你開始讀之前要看的文件、讀 source 的順序、驗收標準整理成一條可以照著走的路線。

## 先確認你可以進 `04`

開始前先確認三件事：

- 你已經完成 `00-03`，而且能回答各 lab README 的「完成後你應該能回答」。
- 你知道 `03` 的 `read/write/ioctl/poll/mmap` 是多條 userspace ABI path，且它們會碰同一份 driver state。
- 你能用自己的話解釋：userspace 不是直接呼叫 C function，而是透過 `/dev/...`、VFS、`file_operations` 進到 driver callback。

如果這三件事還不穩，先回去補：

1. [`../onboarding/01-to-03-user-kernel-abi-bridge.md`](../onboarding/01-to-03-user-kernel-abi-bridge.md)
2. [`../../labs/03-ioctl-poll-mmap/README.md`](../../labs/03-ioctl-poll-mmap/README.md)
3. [`../onboarding/kernel-filesystem-surfaces.md`](../onboarding/kernel-filesystem-surfaces.md)
4. [`../onboarding/kernel-api-parameter-roles.md`](../onboarding/kernel-api-parameter-roles.md)

## 正確閱讀順序

1. 讀 [`../onboarding/03-to-05-concurrency-pci-bridge.md`](../onboarding/03-to-05-concurrency-pci-bridge.md) 的 `03 -> 04` 段落。先理解為什麼 `03` 後不是直接跳 PCI，而是先練 race、shared state 與 cleanup。
2. 讀 [`../concepts/concurrency-primer.md`](../concepts/concurrency-primer.md)。第一輪只抓 `mutex`、`spinlock`、`waitqueue`、`completion` 的用途差異，不要急著背完整 API。
3. 讀 [`lab-04-walkthrough.md`](lab-04-walkthrough.md)。先把 unsafe mode、safe mode、lost update、`expected_at_least` 的白話模型建立起來。
4. 讀 [`../../labs/04-locking-and-races/README.md`](../../labs/04-locking-and-races/README.md)。確認這關的目標、`/dev/driver_lab_race0`、CLI 命令、smoke test 與完成問題。
5. 讀 [`../../labs/04-locking-and-races/driver_lab_race_uapi.h`](../../labs/04-locking-and-races/driver_lab_race_uapi.h) 和 [`../../labs/04-locking-and-races/driver_lab_race_uapi.h.md`](../../labs/04-locking-and-races/driver_lab_race_uapi.h.md)。先確定 `safe-mode`、`status`、`inc`、`reset` 對應哪些 ioctl。
6. 讀 [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c) 和 [`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md)。理解 userspace pthread 如何同時對同一個 fd 送 `DL_RACE_IOC_INC_COUNTER`。
7. 讀 [`../../labs/04-locking-and-races/driver_lab_race.c`](../../labs/04-locking-and-races/driver_lab_race.c) 和 [`../../labs/04-locking-and-races/driver_lab_race.c.md`](../../labs/04-locking-and-races/driver_lab_race.c.md)。照下面的 source trace 順序讀，不要從第一行硬掃。
8. 讀 [`../../labs/04-locking-and-races/test.sh`](../../labs/04-locking-and-races/test.sh) 和 [`../../labs/04-locking-and-races/test.sh.md`](../../labs/04-locking-and-races/test.sh.md)。確認 smoke test 是 unsafe/safe 對照，不是 race correctness 的數學證明。
9. 最後讀 [`../../labs/04-locking-and-races/debug-checklist.md`](../../labs/04-locking-and-races/debug-checklist.md) 和 [`../reference/debugging-playbook.md`](../reference/debugging-playbook.md)。遇到數字不直覺、`rmmod` 卡住、device node 不見時，用它們定位問題。

## 三輪閱讀法

Lab04 的第一輪邊界很重要：本關實作主線是 `mutex + kthread + ioctl race reproduction`。`spinlock`、`atomic`、`completion`、`workqueue`、KCSAN、lockdep 會先作為語彙與後續方向出現，但不是這個 smoke test 要你立刻完成的內容。

第一輪只看「問題如何被重現」：

- `safe_mode = 0` 為什麼會 lost update。
- `race <threads> <loops>` 如何製造多條 userspace execution paths。
- `expected_at_least` 為什麼只是最低預期，不是精確答案。

第二輪看「mutex 修正了哪一段」：

- `dl_counter` 是共享資料本體。
- unsafe path 把 read-modify-write 拆開且不加鎖。
- safe path 用 `mutex` 讓同一時間只有一條路徑更新 counter。

第三輪看「lifetime 與往後 PCI 的連接」：

- background kthread 也是共享 state 的競爭來源。
- `driver_lab_race_exit()` 必須先停 worker，再拆 char device resource。
- 之後 `05-07` 的 PCI、IRQ、DMA 也會遇到同樣的 shared state 與 teardown 問題。

## Source trace 順序

讀 [`../../labs/04-locking-and-races/driver_lab_race.c`](../../labs/04-locking-and-races/driver_lab_race.c) 時，照這個順序：

1. `dl_counter`、`dl_safe_mode`、`dl_worker_running`：先標出 shared state。
2. `dl_race_increment_unlocked()`：看故意做壞的 read-modify-write。
3. `dl_race_increment_locked()`：看 mutex 保護下的最小修正版。
4. `dl_race_increment()`：看 safe/unsafe 模式切換點，並確認 `READ_ONCE()` 不是 lock。
5. `dl_race_worker_fn()`：看 background kthread 如何模擬 driver 內部也會碰 shared state。
6. `dl_race_ioctl()`：把 CLI subcommand 對到 ioctl command。
7. `driver_lab_race_init()`：看 `/dev/driver_lab_race0` 與 worker 如何建立。
8. `driver_lab_race_exit()`：看為什麼要先停 worker，再清 device/class/cdev/major-minor。

## 第一輪可以先略過

- `spinlock`、`atomic`、`completion`、`workqueue` 的完整使用策略。
- KCSAN、lockdep、KASAN 的實戰設定。
- PREEMPT_RT 下各種 lock semantic 的細節。
- pthread scheduling 為什麼每次結果不完全一樣。
- PCI IRQ context 下該如何選 lock。

先把「誰和誰競爭同一份 state」講清楚，比先背所有工具重要。

## 實驗驗收方式

手動示範順序：

```sh
cd labs/04-locking-and-races
make
cc -Wall -Wextra -Werror -pthread -o ../../tests/driver_lab_race_cli ../../tests/driver_lab_race_cli.c
sudo insmod ./driver_lab_race.ko
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
sudo rmmod driver_lab_race
```

自動 smoke test：

```sh
./labs/04-locking-and-races/test.sh
```

觀察重點：

- unsafe mode 的 `observed` 通常會比 `expected_at_least` 差很多。
- safe mode 通常會更接近或超過 `expected_at_least`。
- safe mode 不需要等於 `threads * loops`，因為 background worker 也會增加 counter。
- smoke test 的 gate 是 `safe_observed` 不應小於 `unsafe_observed`。

## 完成後你應該能回答

| 問題 | 標準答案方向 |
|---|---|
| `04` 為什麼接在 `03` 後面？ | `03` 已有多條 ABI path 共享 state；`04` 讓你先在沒有硬體的情境下練 race 與 cleanup。 |
| 共享 state 是誰？ | 第一輪先回答 `dl_counter`；延伸還有 `dl_safe_mode`、`dl_worker_running`。 |
| unsafe mode 為什麼會 lost update？ | `dl_race_increment_unlocked()` 把讀 counter、等待、寫回拆開，且沒有 lock。 |
| safe mode 修了哪裡？ | 用 `mutex` 保護 `dl_counter++` 這段 critical section。 |
| 為什麼 `expected_at_least` 不是精確答案？ | userspace increment 之外，background kthread 也會增加 counter。 |
| 為什麼 unload 要先停 worker？ | 避免背景 thread 在 device/class/cdev/major-minor teardown 後繼續碰 driver state。 |
| 什麼時候才需要深入 KCSAN 或 lockdep？ | 第一輪能說清 shared state、競爭路徑、mutex 修正後，再進階用工具找 data race 或 lock ordering 問題。 |

## 官方查證入口

- [Lock types and their rules](https://docs.kernel.org/locking/locktypes.html)
- [Generic Mutex Subsystem](https://docs.kernel.org/locking/mutex-design.html)
- [Driver Basics](https://docs.kernel.org/driver-api/basics.html)
- [Kernel Concurrency Sanitizer](https://docs.kernel.org/dev-tools/kcsan.html)
- [Kernel Testing Guide](https://docs.kernel.org/dev-tools/testing-overview.html)
