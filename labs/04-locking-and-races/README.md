# 04 - Locking and Races

## 目標

在沒有硬體的情況下，先把 driver 最容易出事的同步與 lifetime 問題練掉。

> [!NOTE]
> 這是進階關卡。你還沒把 `00-03` 做熟之前，不需要急著碰這一關。

## 開始前先看

- [`../../docs/concurrency-primer.md`](../../docs/concurrency-primer.md)

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

## 第一版先只要求你做到

- 你能重現 race
- 你能說出共享資料是什麼
- 你知道為什麼 `mutex` 能先解掉第一層問題

## 之後才補的東西

- KCSAN / lockdep 實戰
- 更進階的 lifetime 問題
- 與 IRQ path 混合的同步問題

## 新手先記住這一關在補什麼

- 單執行緒能跑，不代表多執行緒安全
- driver 常死在 race、lifetime、cleanup，不是死在語法
