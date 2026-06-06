# 04 - Locking and Races

## 目標

在沒有硬體的情況下，先把 driver 最容易出事的同步與 lifetime 問題練掉。

> [!NOTE]
> 這是進階關卡。你還沒把 `00-03` 做熟之前，不需要急著碰這一關。

## 開始前先看

- [`../../docs/onboarding/03-to-05-concurrency-pci-bridge.md`](../../docs/onboarding/03-to-05-concurrency-pci-bridge.md)
- [`../../docs/concepts/concurrency-primer.md`](../../docs/concepts/concurrency-primer.md)
- [`../../docs/guides/lab-04-walkthrough.md`](../../docs/guides/lab-04-walkthrough.md)

## 先備條件

- 你已經寫過至少一支會被 userspace 反覆呼叫的 driver
- 你已經知道 `read/write/ioctl` 會共享同一份 kernel state

## 這一關要練什麼

- mutex
- spinlock
- atomic
- completion
- waitqueue
- workqueue
- kthread
- KASAN / KCSAN / lockdep

## 建議輸出

- 一個刻意可重現 race 的版本
- 一個修正後版本
- 測試腳本可證明修正前後差異

## 這一關最小白話情境

你可以先把這一關想成：

- thread A 在 `write()`
- thread B 在 `ioctl()`
- 兩邊都碰同一份 shared state

如果沒有同步機制，就可能出現：

- counter 不一致
- buffer 內容錯亂
- cleanup 太早發生

## 第一次不要急著寫完整 driver

對新手最合理的步驟是：

1. 先做一個「故意會壞」的版本
2. 用多執行緒 userspace 測試把它踩爆
3. 再用 `mutex` 修到穩
4. 最後才加更進階的 waitqueue / completion / worker

## 如果你完全看不懂 source code，先看這 5 行/區塊

1. `dl_counter`：這是被多條路徑共享的 counter。
2. `dl_safe_mode`：這個開關決定目前示範 unsafe 還是 safe。
3. `dl_race_increment_unlocked()`：故意不加鎖，讓 lost update 容易出現。
4. `dl_race_increment_locked()`：用 `mutex` 保護同一段 increment。
5. `driver_lab_race_exit()`：先停背景 worker，再清 device resource。

## 這一關現在已實作的介面

module 載入後會建立：

```text
/dev/driver_lab_race0
```

## Source 旁讀文件

讀 source 時可以直接打開同目錄的 companion doc，不需要回到 `docs/` 裡找對應解釋：

| Source | 旁讀文件 | 建議用途 |
|---|---|---|
| [`driver_lab_race.c`](driver_lab_race.c) | [`driver_lab_race.c.md`](driver_lab_race.c.md) | 逐段理解 unsafe/safe increment、mutex、kthread、ioctl control path 與 cleanup。 |
| [`driver_lab_race_uapi.h`](driver_lab_race_uapi.h) | [`driver_lab_race_uapi.h.md`](driver_lab_race_uapi.h.md) | 理解 `struct dl_race_status` 與 `DL_RACE_IOC_*` ABI。 |
| [`Makefile`](Makefile) | [`Makefile.md`](Makefile.md) | 理解 Lab04 external module kbuild 與 CLI build 分工。 |
| [`test.sh`](test.sh) | [`test.sh.md`](test.sh.md) | 理解 smoke test 如何對照 unsafe/safe mode。 |
| [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c) | [`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md) | 理解 userspace pthread 如何對 driver ioctl 施壓。 |

## 這一關會出現哪些 filesystem 入口

`04` 的重點是 race，不是新 device model；filesystem 入口仍沿用 `02` 的 char device 模型。

| 路徑 | 第一輪用途 |
|---|---|
| `/dev/driver_lab_race0` | CLI 對 driver 做 `status/reset/safe-mode/inc/race` 的操作入口。 |
| `/sys/class/driver_lab_race/driver_lab_race0` | 確認 race device 的 class/device entry 已建立。 |
| `/sys/devices/virtual/driver_lab_race/driver_lab_race0` | 常見的 virtual device 實際 sysfs 位置。 |
| `/proc/devices` | 輔助確認 `driver_lab_race` 的 major number 已註冊。 |

如果 `/dev/driver_lab_race0` 沒出現，先看 `dmesg`；再查 `/sys/class/driver_lab_race/` 是否存在，用來分辨是 driver init 失敗，還是 `/dev` node 層問題。

搭配的 userspace 工具：

- [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c)

這支工具目前能做：

- `status`
- `reset`
- `safe-mode 0|1`
- `inc <count>`
- `race <threads> <loops>`

## 第一版先只要求你做到

- 你能重現 race
- 你能說出共享資料是什麼
- 你知道為什麼 `mutex` 能先解掉第一層問題

## 之後才補的東西

- KCSAN / lockdep 實戰
- 更進階的 lifetime 問題
- 與 IRQ path 混合的同步問題

## 這一關的教學設計

這個 lab 刻意提供兩種模式：

1. `safe_mode = 0`
   - 故意不用 lock 保護 increment
   - 比較容易踩出 lost update
2. `safe_mode = 1`
   - 用 `mutex` 保護 increment
   - 用來對照 race 被修掉後的結果

## 使用方式

```sh
make
cc -Wall -Wextra -Werror -pthread -o ../../tests/driver_lab_race_cli ../../tests/driver_lab_race_cli.c
sudo insmod ./driver_lab_race.ko
../../tests/driver_lab_race_cli /dev/driver_lab_race0 status
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
sudo rmmod driver_lab_race
```

## `test.sh` 逐段在驗什麼

1. 確認目前是 Linux，因為這關要載入 kernel module。
2. `make` 建出 `driver_lab_race.ko`。
3. 用 `cc -pthread` 建出 userspace race CLI。
4. 如果前一次留下同名 module，先卸載，避免背景 worker 狀態混亂。
5. 載入 module，檢查 `/dev/driver_lab_race0`、`/sys/class/driver_lab_race/driver_lab_race0`、`/proc/devices`。
6. 先切到 `safe-mode 0`，reset 後跑 `race 8 50`。
7. 再切到 `safe-mode 1`，reset 後跑同一組 `race 8 50`。
8. 從兩份 log 抽出 `observed=`，確認 safe mode 不應比 unsafe 更差。
9. 卸載 module，確認 sysfs class device 消失，清 build artifact 與暫存 CLI。

這支 test 不是要證明 mutex 讓數字永遠一模一樣，而是用同一組壓力條件對照 unsafe/safe 的差異。

## 你應該觀察到什麼

- 在 `safe_mode = 0` 時：
  - `observed` 常常小於 `expected_at_least`
- 在 `safe_mode = 1` 時：
  - `observed` 會更接近預期值

這就是最基本的 race 對照實驗。

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | unsafe mode 故意示範 lost update；safe mode 用 mutex 保護共享 counter；背景 kthread 也是共享狀態的競爭來源。 |
| 可以先略過 | spinlock、atomic、completion、workqueue 的完整使用時機；KASAN/KCSAN/lockdep 的實戰細節。 |
| 之後再回來補 | process context vs IRQ context 的 lock 選擇、worker lifetime、卸載時如何避免背景工作碰已釋放資源。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這一關的 userspace 入口在哪裡？ | `/dev/driver_lab_race0`；CLI 透過 ioctl 切換模式、reset、increment 與讀 status。 |
| unsafe mode 在示範什麼？ | `safe_mode = 0` 時故意不保護 read-modify-write，讓多條路徑容易造成 lost update。 |
| safe mode 怎麼修正第一層問題？ | `safe_mode = 1` 時用 `mutex` 包住共享 counter 的 increment，讓同一時間只有一條路徑修改它。 |
| 背景 kthread 為什麼重要？ | 它模擬 driver 內部也可能同時碰共享 state；race 不只來自 userspace thread。 |
| 第一個觀測點是什麼？ | `driver_lab_race_cli ... race <threads> <loops>` 的 `expected_at_least` 與 `observed` 差異。 |
| 這一關主要拿到什麼 resource？ | char device resource 與一條背景 kthread。 |
| cleanup 要先做什麼？ | 卸載時先停背景 worker，再移除 device/class/cdev/major-minor，避免 thread 繼續碰已拆掉的資源。 |
| race 結果看起來怪時第一個看哪裡？ | 先確認目前 `safe_mode`，再回頭看 `expected_at_least` 的定義與 `dmesg`。 |

## 一次合理的示範輸出

下面只是示意，不是唯一正確數字：

```text
$ ../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
expected_at_least=400 observed=237 safe_mode=0

$ ../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
expected_at_least=400 observed=412 safe_mode=1
```

你第一次不用追求每次都一模一樣。

這一關重點是：

- unsafe 結果通常偏差更大
- safe 結果通常更合理

## 第一次卡住先看哪裡

- 如果 `insmod` 失敗：
  - 先看 [`../../docs/reference/common-failures.md`](../../docs/reference/common-failures.md)
- 如果 `/dev/driver_lab_race0` 沒出現：
  - 先看 `dmesg`
- 如果 `race` 指令跑完數字很奇怪：
  - 先回去看 [`../../docs/guides/lab-04-walkthrough.md`](../../docs/guides/lab-04-walkthrough.md) 裡對 `expected_at_least` 的解釋

## 新手先記住這一關在補什麼

- 單執行緒能跑，不代表多執行緒安全
- driver 常死在 race、lifetime、cleanup，不是死在語法

## 看 source code 時先抓哪幾個點

這一關要刻意看到「錯」與「修正」的對照：

1. `dl_counter`、`dl_safe_mode`、`dl_worker_running`：先找出哪些 state 被多條路徑共享
2. `dl_race_increment_unlocked()`：故意拆開 read-modify-write，讓 race 容易重現
3. `dl_race_increment_locked()`：用 `mutex` 保護同一個 counter 的最小修正版
4. `dl_race_ioctl()`：userspace 如何切換 safe mode、reset、讀 status
5. `dl_race_worker_fn()`：背景 kthread 如何模擬 driver 內部也會同時改 state
6. `driver_lab_race_exit()`：卸載時為什麼要先停 worker，再清 device 資源

遇到 kernel API 時，先套用「參數角色」模板，完整方法見 [`../../docs/onboarding/kernel-api-parameter-roles.md`](../../docs/onboarding/kernel-api-parameter-roles.md)。

| API | 參數角色 | 第一輪理解 |
|---|---|---|
| `mutex_lock(&dl_race_lock)` | lock pointer | 取得保護共享 counter 的 lock；這關在 ioctl/kthread path 使用一般 mutex。 |
| `kthread_run(dl_race_worker_fn, NULL, "dl_race_worker")` | thread function、private data、名稱 | 建一條背景 kernel thread；`NULL` 表示本 lab 沒傳 private data。 |
| `copy_from_user(&safe_mode, (void __user *)arg, sizeof(safe_mode))` | kernel destination、userspace source、size | 從 ioctl arg 讀回 userspace 想設定的 safe mode。 |
| `copy_to_user((void __user *)arg, &status, sizeof(status))` | userspace destination、kernel source、size | 把 counter/safe_mode/worker 狀態回傳給 CLI。 |
| `struct dl_race_status` | UAPI struct | userspace CLI 和 kernel driver 都要同意欄位順序與型別。 |

你不需要在第一輪就理解所有 kernel concurrency primitive。先把 `mutex` 解決 lost update 的原因講清楚。
