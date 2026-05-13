# 閱讀地圖

這份文件只回答一件事：

> 這個 repo 很大時，完全新手到底該先看什麼，什麼時候才需要看後面的東西？

## 先看哪三份

先只看下面三份，不要一開始就打開所有 lab：

1. [`beginner-primer.md`](beginner-primer.md)
2. [`linux-host-setup.md`](linux-host-setup.md)
3. [`check-kernel-env-explained.md`](check-kernel-env-explained.md)

這三份分別在補：

- 你現在站在哪一層
- Linux host 需要哪些條件
- 第一次看到環境檢查輸出時，怎麼判斷自己缺了什麼

## 第一天只做到哪裡

第一天只要求你做到：

1. 能在 Linux 環境看懂 `scripts/check-kernel-env.sh` 的輸出
2. 完成 `00-hello-module`
3. 知道 `insmod`、`rmmod`、`dmesg`、`debugfs` 分別在扮演什麼角色

如果 `00` 都還沒穩，不要先讀 QEMU 文件。

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
