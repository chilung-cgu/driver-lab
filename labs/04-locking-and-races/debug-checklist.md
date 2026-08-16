# Lab04 Debug Checklist

這份清單排查race reproduction、safe/unsafe對照、worker lifetime與module unload。先保存原始輸出與`dmesg`，不要只重跑到它偶然成功。

## Unsafe與safe看起來差不多

每輪先切mode並reset：

```sh
CLI=../../tests/driver_lab_race_cli
DEV=/dev/driver_lab_race0

sudo "$CLI" "$DEV" safe-mode 0
sudo "$CLI" "$DEV" reset
sudo "$CLI" "$DEV" race 8 50

sudo "$CLI" "$DEV" safe-mode 1
sudo "$CLI" "$DEV" reset
sudo "$CLI" "$DEV" race 8 50
```

可能原因：

- race是timing-dependent，當次interleaving未暴露lost update；
- thread/loop太少；
- background worker同時increment，使absolute count不等於`threads * loops`；
- 忘了在兩輪間reset；
- CLI/module不是同一次build；
- mode切換或ioctl失敗但輸出被忽略。

先用：

```sh
sudo "$CLI" "$DEV" status
sudo dmesg | tail -n 100
```

有限次未觀察到錯誤不代表沒有data race；source推理、KCSAN與stress回答不同層級的問題。

## `safe mode should not perform worse than unsafe mode`

Current smoke test只做probabilistic teaching gate：safe observed不應小於unsafe observed。失敗時：

1. `status`確認safe mode真的為1；
2. 確認兩輪前都有reset；
3. 確認沒有舊module/CLI；
4. 保存兩輪完整output，不要只截最後數字；
5. 加大threads/loops重做diagnostic，但不要因此把test改成「重試直到pass」；
6. 檢查第一個kernel/userspace error。

即使safe比較大，也只表示這個workload下mutex path改善lost update，不是對所有concurrency property的證明。

## Load後counter或狀態不合理

Current corrected source應先初始化shared state，再`kthread_run()`。若剛load就出現不可解釋的counter回退：

- 確認跑的是audit branch的module；
- 看`driver_lab_race_init()`是否在thread start後又清state；
- 確認沒有另一個舊module仍載入；
- 保存load後第一次`status`與dmesg timestamp。

Thread一旦start即可立即執行；「之後才初始化」是startup race。

## `rmmod`卡住或失敗

```sh
lsmod | grep '^driver_lab_race'
ps -ef | grep driver_lab_race_cli
sudo dmesg | tail -n 100
```

可能原因：

- process仍開著device fd，module reference未歸零；
- worker未經`kthread_stop()`同步退出；
- worker卡在不可中斷或長時間操作；
- cleanup先拆resource，仍有execution path在使用；
- test cleanup吞掉了前一個錯誤。

正確關係是：

```text
拒絕/停止新工作
→ kthread_stop並等待function返回
→ 再destroy device/class/cdev/devt
```

只設一個bool後立刻free不是等價替代。

## 不知道用mutex還是spinlock

先問：

1. 這把lock會在哪些context取得？
2. 任一路徑是否在hard IRQ/softirq或持有raw spinlock等不可睡context？
3. 臨界區是否會呼叫可能睡眠的API？
4. 是否可把shared state拆成per-context/per-queue，降低共享？

第一輪：

- 全部是可睡process context且臨界區可睡：mutex；
- hard IRQ會取得同一lock：不能用mutex，通常需短spinlock-based設計；
- process與local IRQ共享同一spinlock：process側常需`spin_lock_irqsave()`，但仍要依context與PREEMPT_RT規則確認；
- 不要用atomic或`READ_ONCE()`假裝保護multi-field invariant。

## 工具選擇

- 數值偶發錯、懷疑未同步access：KCSAN + stress；
- 鎖順序、IRQ-safe/unsafe inversion：lockdep；
- unload後UAF/OOB：KASAN；
- 想看哪條path執行：dynamic debug/ftrace；
- 想證明mutex修正哪個invariant：先做source-level interleaving推理。
