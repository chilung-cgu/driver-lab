# Companion Docs Rollout Plan

這份計畫用來追蹤 source companion docs 的分段導入。原則是：只對「學習時會需要 trace 的 source/build/test 檔案」加長篇旁讀；重複性很高或只轉呼叫其他工具的 wrapper，先不寫長篇，避免文件量失控。

## 判斷標準

需要長篇 companion doc：

- kernel driver `.c`
- kernel/userspace 共用 ABI `.h`
- userspace runtime / CLI `.c` / `.h`
- 每個 lab 的主要 `test.sh`
- 每種不同型態的 `Makefile`
- QEMU / script 中有實際流程、環境判斷或風險處理的 shell script

通常不需要長篇 companion doc：

- 每個 lab 重複轉呼叫 repo-level 檢查的 `quality.sh`
- 純索引或已經是教學入口的 README
- 空模板或只提供填寫格式的 notes

## 分段順序

| Phase | 範圍 | 理由 | 狀態 |
|---|---|---|---|
| 0 | companion-doc skill、Lab03 golden sample | 建立品質標準 | Done |
| 1 | Lab00 `driver_lab_hello.c` / `Makefile` / `test.sh` | 最小 build/load/unload 閉環，是所有後續 lab 的基礎 | Done |
| 2 | Lab01 debugfs logging | 補觀測面與 debugfs 心智模型 | Done |
| 3 | Lab02 char device | 補 `/dev`、cdev、read/write、filesystem surface | Done |
| 4 | Lab04 locking/race 與 race CLI/UAPI | 補 concurrency、race reproduction、safe mode | Done |
| 5 | Lab05-07 QEMU EDU PCI/MMIO/IRQ/DMA | 難度最高，需要更深圖解、resource lifecycle、QEMU/PCI 背景 | Done |
| 6 | Lab08-09 runtime/stress | 補驗證策略、stress scripts、runtime-library lab intent | Done |
| 7 | repo-level scripts and qemu helpers | 補環境檢查、filesystem helper、QEMU launch flow | Done |

## 驗證規則

每個 phase 完成前至少執行：

```sh
./scripts/quality.sh .
make -C runtime clean all
```

涉及 Linux-only module 行為的 phase，視範圍在 `s2:/home/ubuntu/driver-lab` 跑對應 lab smoke test。

## Git 規則

- 每個 phase 至少一個 commit。
- 不使用 `git add .`；按 phase 明確 stage。
- commit message 使用 Conventional Commits，主旨包含中文。
