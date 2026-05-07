# 04 - Locking and Races

## 目標

在沒有硬體的情況下，先把 driver 最容易出事的同步與 lifetime 問題練掉。

> [!NOTE]
> 這是進階關卡。你還沒把 `00-03` 做熟之前，不需要急著碰這一關。

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

## 新手先記住這一關在補什麼

- 單執行緒能跑，不代表多執行緒安全
- driver 常死在 race、lifetime、cleanup，不是死在語法
