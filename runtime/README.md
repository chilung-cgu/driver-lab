# runtime

這裡放 user-space runtime 的練習骨架。

> [!NOTE]
> 如果你目前還停在 `00` 或 `01`，先把這個目錄當成「之後會用到的輔助角色」即可，不需要現在就完全看懂。

## 目前內容

- `include/driver_lab_runtime.h`
- `src/driver_lab_runtime.c`
- `Makefile`

目前這份 runtime 已封裝：

- 開啟裝置
- 關閉裝置
- 讀取
- 寫入
- `ioctl`
- `poll`
- `mmap`

它目前對應：

- [`../labs/02-char-device`](../labs/02-char-device)
- [`../labs/03-ioctl-poll-mmap`](../labs/03-ioctl-poll-mmap)

## 為什麼現在就要有 runtime

很多新手會把 driver 學習切成：

- kernel module 一邊
- userspace 測試一邊

但對真實的 accelerator host driver 來說，這兩邊本來就是一起長出來的。

所以這裡故意很早就放一個最小 runtime，讓你開始建立這個觀念：

- kernel driver 定義 ABI
- runtime 負責包裝 ABI
- CLI / test 再去呼叫 runtime

## 現在應該怎麼用

1. 先完成 [`../labs/02-char-device`](../labs/02-char-device)
2. 在 `runtime/` 執行 `make`
3. 用 [`../tests/driver_lab_char_cli.c`](../tests/driver_lab_char_cli.c) 驗證讀寫與 `ioctl/poll/mmap`

如果你現在看不懂 `runtime/`，不用硬啃。它的主要目的，是讓你開始意識到 driver 工作通常不只 kernel module 本體。

## 目前還沒完成的部分

- timeout / retry 策略仍很初階
- 還沒有把 error mapping 收斂成更穩定的 runtime 層
- 還沒有針對 PCIe/QEMU EDU driver 補專用 helper
