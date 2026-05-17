# 0 基礎學習儀表板

這份文件用來回答：

> 我現在該做哪一關？做到什麼程度才算可以前進？

它不是新的長篇教學，而是你每次打開 repo 時用來定位進度的檢查表。

## 先分清楚三種檢查

| 檢查 | 用途 | 代表什麼 |
|---|---|---|
| `scripts/check-kernel-env.sh` | 檢查 Linux host / guest 是否有基本 kernel module 開發條件 | 環境大致可用，不代表 driver 正確 |
| `scripts/quality.sh .` | 檢查 shell 語法、Markdown 連結、可選 shellcheck / checkpatch | repo hygiene 沒明顯壞掉 |
| `labs/*/test.sh` | 跑單一 lab 的 build / load / 操作 / unload smoke test | 該 lab 的最小行為路徑可跑 |

`quality.sh` 通過不代表 kernel module 已驗證；真正的 driver 行為要看各 lab 的 `test.sh`，而且 `00-07` 必須在 Linux host 或 Linux guest 內跑。

## 階段 0：第一天，只建立最小閉環

| 項目 | 內容 |
|---|---|
| 必讀 | [`beginner-primer.md`](beginner-primer.md)、[`lab-file-roles.md`](lab-file-roles.md)、[`linux-host-setup.md`](linux-host-setup.md) |
| 要跑 | `scripts/check-kernel-env.sh`、`labs/00-hello-module/test.sh` |
| 成功訊號 | `00-hello-module smoke test passed.`，且 `dmesg` 看得到 `driver_lab_hello` |
| 前進條件 | 你能說明 `insmod`、`rmmod`、`module_init()`、`module_exit()`、`dmesg` 各自扮演什麼角色 |

第一天不要碰 QEMU、PCIe、DMA。先把 build / load / unload / log 這個閉環跑穩。

## 階段 1：`00-02` 基礎閉環

| Lab | 必讀 | 成功訊號 | 可以前進前要會講 |
|---|---|---|---|
| `00-hello-module` | [`../../labs/00-hello-module/README.md`](../../labs/00-hello-module/README.md) | module 可 build / load / unload，`dmesg` 有 log | module 沒有 `main()`，入口由 `module_init()` 指定 |
| `01-debugfs-logging` | [`00-to-01-debugfs-bridge.md`](00-to-01-debugfs-bridge.md)、[`../../labs/01-debugfs-logging/README.md`](../../labs/01-debugfs-logging/README.md) | debugfs 檔案可讀寫，counter 會變 | debugfs 是 debug 觀測入口；寫 `trigger` 會進 `dl_trigger_write()`；dynamic debug 是選擇性開 `pr_debug()` |
| `02-char-device` | [`lab-transition-map.md`](lab-transition-map.md)、[`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)、[`../../labs/02-char-device/README.md`](../../labs/02-char-device/README.md) | `/dev/driver_lab_char0` 可 write/read | userspace 的 `read/write` 會走到 driver 的 `file_operations` callback |

完成這階段後，你應該能畫出：

```text
userspace command -> /dev or debugfs -> driver callback -> kernel state -> dmesg/readback
```

## 階段 2：`03-04` ABI 與併發

| Lab | 必讀 | 成功訊號 | 可以前進前要會講 |
|---|---|---|---|
| `03-ioctl-poll-mmap` | [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)、[`../../labs/03-ioctl-poll-mmap/README.md`](../../labs/03-ioctl-poll-mmap/README.md) | `ioctl`、`read`、`mmap-read`、`poll` smoke test 都通過 | data path、control path、event path、shared memory path 差在哪 |
| `04-locking-and-races` | [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md)、[`../concepts/concurrency-primer.md`](../concepts/concurrency-primer.md)、[`../../labs/04-locking-and-races/README.md`](../../labs/04-locking-and-races/README.md) | unsafe / safe mode 有可觀察差異 | lost update 是什麼，為什麼 mutex 能保護共享 counter |

完成這階段後，你應該先停一下整理筆記。若你還無法說明 cleanup path 或 lock 保護哪個 state，不要急著跳 PCIe。

## 階段 3：`05-07` QEMU PCI

| Lab | 必讀 | 成功訊號 | 可以前進前要會講 |
|---|---|---|---|
| `05-pci-edu-mmio` | [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md)、[`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)、[`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md) | `lspci` 看得到 `1234:11e8`，`liveness check passed` | PCI core match ID 後才呼叫 `probe()`，BAR0 是 MMIO register window |
| `06-pci-edu-irq` | [`05-to-07-pci-irq-dma-bridge.md`](05-to-07-pci-irq-dma-bridge.md)、[`../../labs/06-pci-edu-irq/README.md`](../../labs/06-pci-edu-irq/README.md) | `request_irq ok`、`irq self-test passed` | handler 要讀 status、寫 acknowledge，不能只印 log |
| `07-pci-edu-dma` | [`05-to-07-pci-irq-dma-bridge.md`](05-to-07-pci-irq-dma-bridge.md)、[`../../labs/07-pci-edu-dma/README.md`](../../labs/07-pci-edu-dma/README.md) | `round-trip compare passed` | coherent DMA buffer 同時有 CPU pointer 與 device DMA address |

這階段的 driver build / load / smoke test 位置是 Linux guest 或可控制的 Linux 主機，不是 macOS。

## 階段 4：`08-09` runtime 與驗證習慣

| Lab | 必讀 | 成功訊號 | 可以前進前要會講 |
|---|---|---|---|
| `08-runtime-library` | [`../../runtime/README.md`](../../runtime/README.md)、[`../../labs/08-runtime-library/README.md`](../../labs/08-runtime-library/README.md) | `make -C runtime` 可建出 CLI | runtime 是 userspace 封裝層，不是 kernel driver |
| `09-stress-and-fault-injection` | [`../../labs/09-stress-and-fault-injection/README.md`](../../labs/09-stress-and-fault-injection/README.md) | `03` repeated reload / parallel stress 可跑 | stress 是重複施壓，fault injection 是主動讓錯誤路徑發生 |

`09` 目前不是完整 fault injection framework。它的價值是先建立 repeated load/unload、parallel access、每次修改後固定重跑檢查的習慣。

## 每關固定自評法

完成每個 lab 後，不要只看 `test.sh` 有沒有通過。回到該 lab README 的「完成後你應該能回答」，把答案用自己的話講一次。

如果你答不出：

- 入口在哪裡
- 觀測點在哪裡
- 主要 resource 是什麼
- cleanup 怎麼反向釋放
- 失敗時第一個查證點是哪裡

那就先不要前進下一關。這通常代表你只是跑過命令，還沒有建立 driver 心智模型。
