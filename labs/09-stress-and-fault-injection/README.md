# 09 - Stress and Fault Injection

## 目標

把前面做出的 driver 從「能跑」提升到「能驗證」。

> [!NOTE]
> 這一關目前已有第一批可直接執行的 stress 腳本，
> 但故障注入仍需要在真正的 Linux host 上依 kernel 能力逐步補齊。

## 先備條件

- 前面至少已經有一個真正可用的 driver lab
- 你知道正常路徑與 error path 是兩件不同的事

## 這一關要練什麼

- repeated load / unload
- parallel open / close
- long-running stress
- `failslab`
- `fail_page_alloc`
- `fail_usercopy`
- regression 紀律

## 成功標準

- 有 smoke / stress / regression 分層
- 有故障注入腳本
- 有 cleanup path 驗證

## 新手先記住這一關在補什麼

- 「能跑一次」不等於「driver 寫對了」
- 真正難的是 repeated load/unload、失敗注入、cleanup 對稱性

## 目前已完成的部分

- 針對 `03-ioctl-poll-mmap` 的 repeated load/unload 壓力腳本
- 針對 `03-ioctl-poll-mmap` 的 parallel access 壓力腳本

## 目前還沒完成的部分

- `failslab` / `fail_page_alloc` / `fail_usercopy` 的自動化腳本
- 更長時間的 regression matrix
- 針對 QEMU EDU labs 的專用 stress 套件

## 新手第一次不要追的東西

- 一開始不要追非常長時間的 soak test
- 先把「可以穩定重複 20 次」做好
- 先把 repeated load/unload 與 parallel access 練熟

## 第一次理想上要做到的最小版本

第一次只要求你做到：

1. repeated load/unload 可以連跑 20 次
2. parallel access 可以穩定重現與觀察結果
3. 每次失敗時知道要回頭看哪一份 log

## 第一次卡住先看哪裡

- repeated load/unload 偶發失敗
  - 優先懷疑 cleanup path 不對稱
- parallel 測試結果不穩
  - 先分清楚這是教學上刻意示範 race，還是 module 本身壞掉
- 不知道 stress 跟 regression 差在哪
  - stress 是「重複施壓」
  - regression 是「每次修改後都應該固定重跑的檢查」
