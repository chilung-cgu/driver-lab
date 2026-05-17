# 閱讀地圖

這份文件只回答一件事：

> 這個 repo 很大時，完全新手到底該先看什麼，什麼時候才需要看後面的東西？

## 先看哪幾份

先只看下面幾份，不要一開始就打開所有 lab：

1. [`learning-dashboard.md`](learning-dashboard.md)
2. [`beginner-primer.md`](beginner-primer.md)
3. [`lab-file-roles.md`](lab-file-roles.md)
4. [`linux-host-setup.md`](linux-host-setup.md)

這四份分別在補：

- 你現在在整條學習路線的哪個階段
- 你現在站在哪一層
- lab 目錄裡每種檔案扮演什麼角色
- Linux host 需要哪些條件

接著再看 [`check-kernel-env-explained.md`](check-kernel-env-explained.md)，用來理解第一次環境檢查輸出。

## 第一天只做到哪裡

第一天只要求你做到：

1. 能在 Linux 環境看懂 `scripts/check-kernel-env.sh` 的輸出
2. 完成 `00-hello-module`
3. 回答 `00` README 裡「完成後你應該能回答」的標準問題
4. 知道 `insmod`、`rmmod`、`dmesg` 分別在扮演什麼角色

如果 `00` 都還沒穩，不要先讀 QEMU 文件。

## 第一週做到哪裡

第一週的合理目標是完成 `00-02`：

1. `00`：最小 build / load / unload / dmesg 閉環
2. [`00 到 01：debugfs 過渡導讀`](00-to-01-debugfs-bridge.md)：先補 debugfs / VFS callback / dynamic debug 的最低心智模型
3. `01`：debugfs 與 logging 觀測
4. [`Lab 過渡地圖`](lab-transition-map.md)：確認每一關為什麼接下一關
5. [`01 到 03：user-kernel ABI 過渡導讀`](01-to-03-user-kernel-abi-bridge.md)：先補 `/dev`、char device、`ioctl/poll/mmap` 的最低心智模型
6. `02`：`/dev`、`read/write`、`file_operations`

每一關都要回到 README 的「完成後你應該能回答」。如果只會照抄命令，但答不出入口、觀測點、cleanup 與失敗查證點，就先不要前進。

`03` 開始不建議直接跳 source code。先讀 bridge，再回到 README 的 source reading order。

## 什麼時候才看 QEMU

請等到下面三件事至少成立兩件，再進 QEMU：

- 你已完成 `00-02`
- 你知道 `probe/remove` 是裝置生命週期入口
- 你已接受 `05-07` 的實際驗證位置是 `Linux guest`，不是 `macOS`

建議順序：

1. [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)
2. [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
3. [`../../qemu/README.md`](../../qemu/README.md)
4. [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)

## Walkthrough 和 Checklist 差在哪裡

- [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)
  - 給第一次進 guest 的人
  - 會解釋每一步為什麼要做
  - 適合你還不知道卡在哪一層時使用
- [`../guides/linux-guest-05-to-07-checklist.md`](../guides/linux-guest-05-to-07-checklist.md)
  - 給已經跑過一次的人
  - 只保留最短的執行順序與成功訊號
  - 適合第二次、第三次重跑時速查

不要把 checklist 當教學主文件。

## 哪些是學習主線，哪些只是 repo workflow

### 學習主線

- `docs/onboarding/`
- `docs/concepts/`
- `docs/guides/`
- `labs/`
- `runtime/`
- `tests/`
- `qemu/`

### Workflow / Meta

- [`../workflow/ai-agent-git-checkpoint-policy.md`](../workflow/ai-agent-git-checkpoint-policy.md)
- `.githooks/`
- [`../../scripts/install-git-hooks.sh`](../../scripts/install-git-hooks.sh)

這些 workflow 文件不是 driver 教學本體。
如果你只是要學 driver，可以先略過。
