# tests

這裡放 user-space 測試程式與後續 smoke / stress / regression 測試。

## 目前內容

- `driver_lab_char_cli.c`：配合 `runtime/`、`02-char-device`、`03-ioctl-poll-mmap` 的最小 CLI
- `driver_lab_race_cli.c`：配合 `04-locking-and-races` 的 race 重現與 safe-mode 驗證工具

如果你現在還看不懂這個 CLI，沒有關係。

它目前的目的只有一個：

- 讓你看到「driver 不只 module 本體，通常還會搭配一個 userspace 測試或 runtime」

## 預期演進

- `03` 已補 `ioctl` / `poll` / `mmap` 基本互動
- `04` 已補 race 重現與 safe-mode 對照工具
- `05-07` 已補 lab 內部的 Linux guest smoke tests
- `09` 補 stress / repeated load-unload / error-path 測試
