# 07 到 09：runtime 與驗證過渡導讀

做完 `07-pci-edu-dma` 後，你已經看過一條很像真 driver 的硬體路徑：

```text
PCI probe -> BAR/MMIO -> IRQ -> DMA -> cleanup
```

接著的 `08/09` 不是要突然換題目，而是補兩個真實工程會馬上遇到的問題：

1. userspace 要怎麼穩定使用 driver ABI？
2. driver 不能只跑一次成功，要怎麼反覆驗證？

## `08` 不是 kernel driver

`08-runtime-library` 是 userspace runtime。

它不會產生 `.ko`，也不會被 `insmod` 載入。它的目標是把前面 labs 的 userspace 呼叫包成比較一致的 C API。

第一輪先記：

- driver 定義 ABI。
- UAPI header 定義 kernel/userspace 都同意的 struct 與 command。
- runtime include UAPI，呼叫 `open/read/write/ioctl/poll/mmap`。
- CLI 或 app 呼叫 runtime，不要到處散寫 raw syscall。

## 為什麼 `08` 目前主要服務 `02/03`？

因為 `02/03` 已經有 `/dev` device node 與 userspace ABI：

- `02`：`read/write`
- `03`：`read/write/ioctl/poll/mmap`

`05-07` 目前主要是 probe-time self-test，還沒有設計 userspace-visible ABI。不要誤以為 runtime 已經能操作 EDU MMIO/IRQ/DMA driver。

## `09` 不是「全部測試框架都完成」

`09-stress-and-fault-injection` 目前 repo 已有的是：

- `03` repeated load/unload stress
- `03` parallel access stress

它尚未完成：

- KUnit
- kselftest
- `failslab`
- `fail_page_alloc`
- `fail_usercopy`
- `05-07` 專用長時間 regression matrix

這不是缺陷被隱藏，而是目前成熟度被明確標出來。

## stress、regression、fault injection 差在哪？

| 名詞 | 第一輪理解 |
|---|---|
| smoke test | 跑最小成功路徑，確認基本功能沒壞 |
| stress | 重複或並行施壓，讓偶發問題比較容易出現 |
| regression | 每次修改後固定重跑，避免舊功能被改壞 |
| fault injection | 主動讓 allocation/usercopy 等錯誤發生，驗 error path |

第一輪先把 smoke/stress 分清楚。fault injection 和 KUnit/kselftest 可以之後再補。

## KUnit 與 kselftest 第一輪怎麼分？

| 工具 | 第一輪理解 |
|---|---|
| KUnit | kernel 內的白箱單元測試，適合測可拆出來的 helper logic |
| kselftest | userspace-driven regression，適合測已 boot kernel 的對外行為 |

這個 repo 目前還沒有把 driver logic 拆到很適合 KUnit 的形狀，所以先不要急著把 `09` 擴成完整 KUnit project。

## 進 `08/09` 前你要能回答

| 問題 | 標準答案 |
|---|---|
| runtime 為什麼不是 kernel driver？ | runtime 是 userspace library/CLI 輔助層，呼叫 `open/read/write/ioctl/poll/mmap`；它不會產生 `.ko`，也不會被 `insmod` 載入。 |
| runtime 和 UAPI header 的關係是什麼？ | UAPI header 定義 kernel/userspace 都要同意的 ABI；runtime include 這份 header，將 raw syscall 包成較一致的 C API。 |
| `08` 目前主要包哪幾個 lab 的 ABI？ | 目前主要包 `02-char-device` 的 `read/write`，以及 `03-ioctl-poll-mmap` 的 `ioctl/poll/mmap` 路徑。 |
| repeated load/unload 在驗什麼？ | 主要驗 init/exit cleanup 是否對稱，避免多次載入卸載後才暴露 resource 洩漏或狀態殘留。 |
| parallel access 在驗什麼？ | 主要驗多個 userspace client 同時打 read/write/ioctl/poll 時，共享狀態和等待路徑是否能承受壓力。 |
| fault injection 和 stress 差在哪？ | stress 是重複或並行施壓；fault injection 是主動讓 allocation/usercopy 等錯誤發生，用來驗 error path 與 rollback。 |

## 第一輪可以先略過

- 產品級 timeout / retry policy。
- 跨版本 ABI compatibility。
- KUnit `.kunitconfig` 與 test suite 寫法。
- kselftest install/run matrix。
- Linux fault injection debugfs 的所有 runtime knobs。

先把 runtime 的角色與 stress/regression 的基本紀律講清楚。
