# Lab Postmortem Template

這份模板不是每個 lab 都必填。

只有在你遇到實際故障、誤判、環境問題、或值得記住的學習事故時，才複製這份檔案來寫紀錄。不要在每個 lab 目錄預先放空白 `postmortem.md`，那會讓新手誤以為它是必讀內容。

## Context

- Lab:
- Date:
- Kernel:
- Host / guest:
- Commit:

## Symptom

你一開始看到什麼現象？

例子：

- `insmod` 失敗
- `/dev/...` 沒出現
- `poll` 沒醒
- `DMA timeout`

## Root cause

最後確認的真正原因是什麼？

不要只寫「測試失敗」。要寫到可以避免下次重犯。

## What evidence confirmed it?

列出證據，不要只寫猜測。

例子：

- `dmesg` 的關鍵行
- `lsmod` / `lspci` / `/proc/interrupts`
- 哪個 command 可穩定重現
- 哪一段 code 對應到問題

## Fix

你做了什麼修正？

如果只是環境修正，也要寫清楚，例如安裝 headers、掛載 debugfs、切到 Linux guest。

## Regression risk

這個修正可能影響什麼？

例子：

- cleanup path
- ioctl ABI
- timeout 行為
- QEMU guest-only path

## What rule should I remember next time?

用一句話寫下規則。

例子：

- 先確認 module 是否真的載入，再 debug `/dev`。
- `pr_info()` 要看 `dmesg`，不是 terminal stdout。
- `05-07` 的行為驗證必須在 Linux guest 內做。
