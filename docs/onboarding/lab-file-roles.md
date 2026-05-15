# Lab 檔案角色導讀

這份文件回答一個很實際的問題：

> 打開一個 `labs/xx-*` 目錄時，這些檔案到底各自負責什麼？新手應該先看哪一個？

## 建議閱讀順序

第一次進一個 lab，照這個順序就好：

1. 先看 `README.md`，確認這一關的目標、環境、操作命令與驗收標準。
2. 再看 driver `.c` 或 runtime `.c`，只先抓入口、資料流、觀測點、cleanup。
3. 接著看 `test.sh`，理解自動化 smoke test 如何重跑 README 的核心步驟。
4. 卡住時看 `debug-checklist.md`，不要一開始就亂猜。
5. 最後才看 `Makefile`、`quality.sh` 這類 repo hygiene 工具。

你不需要第一次就把每個檔案都讀完。

## 常見檔案角色

| 檔案 | 角色 | 第一次需要讀多深 |
|---|---|---|
| `README.md` | 這一關的主教學文件 | 必讀 |
| `driver_*.c` | kernel driver / kernel module 實作 | 先找 init/probe、callback、cleanup |
| `Makefile` | 告訴 kbuild 或 runtime build system 要建什麼 | 先知道 `make` 為什麼能產生 `.ko` 或 CLI |
| `test.sh` | 自動化 smoke test，把手動步驟串起來 | 先看它驗了哪些成功訊號 |
| `quality.sh` | 對單一 lab 轉呼叫 repo 的基本品質檢查 | 知道它不是 driver 行為測試即可 |
| `debug-checklist.md` | 卡住時的查證順序 | 失敗時再看 |
| `*_uapi.h` | kernel/userspace 共用 ABI 定義 | 從 `03/04` 開始需要認真看 |
| `runtime/` | userspace 封裝層 | 做到 `02/03` 後再看 |
| `tests/*.c` | userspace CLI / stress 工具 | 先看 command 對應哪條 driver path |
| `notes/*template.md` | 學習紀錄模板 | 遇到實際問題時才複製使用 |

## `Makefile` 在 driver lab 裡做什麼

前面幾個 kernel module lab 的 `Makefile` 都在做同一件事：

```text
make
  -> 呼叫目前 Linux kernel 的 kbuild
  -> kbuild 讀 obj-m
  -> 產生 .ko module
```

你現在先記住：

- `obj-m`：這個 lab 要建成哪個外掛 module。
- `KDIR`：目前 Linux kernel 對應的 build tree。
- `M=$(PWD)`：告訴 kbuild 外掛 module 的 source 在目前目錄。
- `make clean`：刪掉 kbuild 產生的暫存檔與 `.ko`。

不要自己用 `gcc driver.c` 編 kernel module。外掛 module 要交給 kbuild。

## `test.sh` 在 driver lab 裡做什麼

`test.sh` 是 smoke test，不是完整產品級測試。

它通常做這幾件事：

1. 確認目前是在 Linux。
2. build module 或 runtime。
3. 如果舊 module 還載著，先卸載。
4. `insmod` 載入 module。
5. 執行最小操作，例如寫入 `/dev/...`、讀 debugfs、觸發 ioctl。
6. 從 `dmesg` 或 CLI 輸出檢查成功訊號。
7. `rmmod` 卸載 module，並清理 build artifact。

所以 `test.sh` 通過，只代表「這一關的最小路徑目前可跑」。它不代表 driver 已經產品級完整。

## `quality.sh` 和 `test.sh` 差在哪裡

`quality.sh` 偏 repo hygiene：

- shell 語法檢查
- 可選的 shellcheck
- Markdown 相對連結檢查
- 可選的 kernel `checkpatch.pl`

`test.sh` 偏 lab 行為：

- build module
- load module
- 跑最小互動
- 看 log 或 CLI 結果
- unload module

簡單講：

```text
quality.sh = 檢查 repo 檔案不要壞
test.sh    = 檢查這一關的最小實驗能不能跑
```

## `debug-checklist.md` 怎麼用

不要把 checklist 當成要背的題庫。

比較好的用法是：

1. 先描述症狀，例如 `insmod failed`、`/dev node 沒出現`、`poll 沒醒`。
2. 再找證據，例如 `dmesg`、`lsmod`、`lspci`、CLI output。
3. 最後對照 checklist 找常見原因。

如果你沒有症狀，只是第一次學，先看 README 和 source code 導讀就好。
