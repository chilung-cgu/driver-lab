# 04 - Locking and Races

## 目標

在沒有硬體的情況下，先把 driver 最容易出事的同步與 lifetime 問題練掉。

> [!NOTE]
> 這是進階關卡。你還沒把 `00-03` 做熟之前，不需要急著碰這一關。

## 開始前先看

- [`../../docs/concurrency-primer.md`](../../docs/concurrency-primer.md)
- [`../../docs/lab-04-walkthrough.md`](../../docs/lab-04-walkthrough.md)

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

## 這一關現在已實作的介面

module 載入後會建立：

```text
/dev/driver_lab_race0
```

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

## 你應該觀察到什麼

- 在 `safe_mode = 0` 時：
  - `observed` 常常小於 `expected_at_least`
- 在 `safe_mode = 1` 時：
  - `observed` 會更接近預期值

這就是最基本的 race 對照實驗。

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
  - 先看 [`../../docs/common-failures.md`](../../docs/common-failures.md)
- 如果 `/dev/driver_lab_race0` 沒出現：
  - 先看 `dmesg`
- 如果 `race` 指令跑完數字很奇怪：
  - 先回去看 [`../../docs/lab-04-walkthrough.md`](../../docs/lab-04-walkthrough.md) 裡對 `expected_at_least` 的解釋

## 新手先記住這一關在補什麼

- 單執行緒能跑，不代表多執行緒安全
- driver 常死在 race、lifetime、cleanup，不是死在語法
