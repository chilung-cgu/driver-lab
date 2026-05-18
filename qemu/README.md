# QEMU Notes

這個目錄是 `05-07` 的 QEMU EDU 操作入口。

> [!NOTE]
> 對完全沒學過 kernel module 的新手，這個目錄一開始可以先略過。
> 你前面真正要先做的是 `00-02`，至少先把 `build / load / unload / debugfs / char device` 練穩。

## 這個目錄負責什麼

- 提供 `QEMU edu` 的 host 端啟動腳本
- 提供 guest bring-up 的最小檢查表
- 指向第一次做 `05-07` 時該讀的文件

## 這個目錄不負責什麼

- 不在 `macOS` 直接 build / load Linux kernel module
- 不取代 guest 內的 `05-07` lab README
- 不保證你的 guest image、套件來源或 distro 流程完全相同

## 支援的執行模型

- `Linux host + Linux guest`
- `macOS host + Linux guest`

兩條路都可以拿來啟動 QEMU。
真正的 driver build / load / smoke test 位置，仍然是 `Linux guest`。

目前 repo 的 `05-07` 已在遠端 Linux host 啟動 QEMU EDU guest 實測通過；這代表流程可重複，但不代表 macOS 可以直接載入 Linux kernel module。換到你的機器時，仍要先確認 guest 內看得到 EDU device。

## 為什麼是 QEMU EDU

QEMU 官方把 `edu` 定位成：

- 教學用 PCI 裝置
- 適合拿來寫 kernel driver
- 明確支援 `MMIO + IRQ + DMA`

## 你在這個階段要做到的事

1. 在 host 上準備 `qemu-system-x86_64`
2. 啟動一台 Linux guest，並把 `edu` 裝置掛進去
3. 在 guest 內確認 `lspci -nn | grep 1234:11e8`
4. 在 guest 內依序完成 `05`、`06`、`07`

## 目前已提供

- [`launch-edu-vm.sh`](launch-edu-vm.sh)：最小可用的 QEMU 啟動腳本
- [`edu-bringup-checklist.md`](edu-bringup-checklist.md)：host 到 guest 的最小 bring-up 清單
- [`../docs/guides/qemu-edu-first-pass.md`](../docs/guides/qemu-edu-first-pass.md)：第一次做 `05-07` 的白話導讀
- [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)：第一次進 guest 的完整 runbook
- [`../docs/guides/linux-guest-05-to-07-checklist.md`](../docs/guides/linux-guest-05-to-07-checklist.md)：第二次之後的速查單

## `launch-edu-vm.sh` 會怎麼選 accelerator

- `Linux`：優先 `kvm`，不可用時退回 `tcg`
- `macOS`：優先 `hvf`，不可用時退回 `tcg`

你也可以手動覆寫：

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 \
QEMU_ACCEL=tcg \
QEMU_EXTRA_ARGS="-monitor stdio" \
./qemu/launch-edu-vm.sh
```

## 建議閱讀順序

1. [`../docs/concepts/pcie-primer.md`](../docs/concepts/pcie-primer.md)
2. [`../docs/guides/qemu-edu-first-pass.md`](../docs/guides/qemu-edu-first-pass.md)
3. [`edu-bringup-checklist.md`](edu-bringup-checklist.md)
4. [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)

## 現在先不要追的東西

- 不要把 `05-07` 混成一支大 driver 一次做完
- 不要一開始追 MSI-X、效能或 reset/AER
- 不要把 Docker 當成 QEMU EDU 的替代方案
