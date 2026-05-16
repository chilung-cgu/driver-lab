# 05 - PCI EDU MMIO

## 目標

使用 QEMU `edu` 裝置完成第一個真正的 PCI driver 起手式。

> [!NOTE]
> 這一關現在已經有第一版可 build 的 driver code 與 smoke test。
> 真正的載入與驗證仍必須在 Linux guest 內完成。

> [!NOTE]
> 如果你現在還不熟 kernel module、debugfs、char device，先不要跳這一關。
> 這一關是前面基礎都站穩後，才開始接近 PCIe host driver 的起點。

## 開始前先看

- [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- [`../../docs/guides/qemu-edu-first-pass.md`](../../docs/guides/qemu-edu-first-pass.md)
- [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)

## 先備條件

- 你已完成 `00-04` 至少前半
- 你知道 `probe/remove` 是 driver 的裝置生命週期入口
- 你接受這一關需要 QEMU 與更多 Linux 背景

## 這一關要練什麼

- `pci_register_driver()`
- `probe/remove`
- `pci_enable_device()`
- BAR resource handling
- `pci_iomap()`
- 讀取裝置 identification / liveness register

## 成功標準

- driver 能 bind `1234:11e8`
- probe 成功
- BAR0 可存取
- 可做基本 liveness check

## 第一次只要先懂這張圖

```mermaid
flowchart LR
    P["kernel PCI scan"] --> Q["driver probe()"]
    Q --> B["BAR0 map"]
    B --> M["read one register"]
```

這一關的最小目標不是寫完整卡 driver，而只是：

- 裝置有沒有被你接手
- BAR 有沒有 map 成功
- 你能不能讀到第一個 register

## 第一次實作順序

1. 先在 guest 內確認 `lspci -nn | grep 1234:11e8`
2. 再讓 driver bind 到 `1234:11e8`
3. 再做 `probe()` log
4. 再做 BAR map
5. 最後才做 liveness register read

## 目前已實作的內容

- `pci_enable_device()`
- `pci_request_region()` / `pci_release_region()`
- `pci_iomap()` / `pci_iounmap()`
- identification register 讀取
- liveness register 的最小自我測試
- Linux guest 用的 smoke test

主要檔案：

- [`driver_lab_edu_mmio.c`](driver_lab_edu_mmio.c)
- [`test.sh`](test.sh)

## 第一次理想上要看到的輸出

```text
$ lspci -nn | grep 1234:11e8
00:04.0 Class 00ff: 1234:11e8
```

`dmesg` 裡第一版通常至少要看到：

```text
driver_lab_edu: probe start
driver_lab_edu: BAR0 mapped
driver_lab_edu: ident=0x....
driver_lab_edu: liveness check passed
```

上面是教學示意，不是要求逐字完全相同。

## 現在怎麼跑

```sh
cd labs/05-pci-edu-mmio
./test.sh
```

這支腳本會做：

1. 確認目前是在 Linux
2. 確認 guest 內真的看得到 `1234:11e8`
3. build module
4. `insmod`
5. 從 `dmesg` 檢查 `probe` / BAR map / liveness log
6. `rmmod`

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `probe()` 什麼時候會被呼叫？ | PCI core 掃到裝置，且 vendor/device ID match `dl_edu_mmio_ids` 後，才呼叫 `dl_edu_mmio_probe()`。 |
| 這一關的硬體入口是什麼？ | QEMU EDU PCI device，PCI ID 是 `1234:11e8`。 |
| BAR0 在這裡是什麼？ | BAR0 是 EDU 的 MMIO register window；`pci_iomap()` 後 driver 可用 `ioread32()` / `iowrite32()` 存取 register。 |
| 第一個觀測點是什麼？ | `lspci -nn | grep 1234:11e8` 與 `dmesg` 裡的 `probe start`、`BAR0 mapped`、`liveness check passed`。 |
| 這一關主要拿到什麼 resource？ | PCI device enable 狀態、BAR0 region、BAR0 MMIO mapping。 |
| cleanup 要釋放哪些東西？ | `pci_iounmap()`、`pci_release_region()`、`pci_disable_device()`。 |
| `probe()` 沒進來時第一個看哪裡？ | 先在 guest 內跑 `lspci -nn | grep 1234:11e8`，確認 QEMU EDU 真的存在。 |

## 先不要急著碰的東西

- MSI-X
- DMA
- reset / AER
- 效能

## 參考

- [`../../qemu/README.md`](../../qemu/README.md)
- [`../../docs/reference/source-index.md`](../../docs/reference/source-index.md)

## 新手先記住這一關在補什麼

- 前面你都在練「沒有真硬體時的共通 driver 技能」
- 這一關開始，你才第一次真的碰到 PCI device discovery 與 MMIO register access

## 看 source code 時先抓哪幾個點

第一次讀 PCI driver，不要先追 PCI core 內部。先看這條最小生命週期：

1. `dl_edu_mmio_ids`：這支 driver 宣告自己要 match 哪個 PCI vendor/device ID
2. `dl_edu_mmio_driver`：告訴 PCI core `probe/remove` 分別是哪個函式
3. `dl_edu_mmio_probe()`：裝置 match 後，driver 如何 enable device、request BAR、map BAR
4. `ioread32()` / `iowrite32()`：CPU 如何透過 MMIO 讀寫 QEMU EDU register
5. `dl_edu_mmio_remove()`：裝置移除或 module 卸載時，如何反向釋放 BAR 與 disable device

這一關的重點是「先安全拿到 BAR0 並做一個最小 register round-trip」，不是設計完整 PCIe accelerator。

## 第一次卡住先看哪裡

- guest 裡看不到 `1234:11e8`
  - 先看 [`../../docs/reference/common-failures.md`](../../docs/reference/common-failures.md)
- `probe()` 沒進來
  - 先檢查 PCI ID table
- BAR map 失敗
  - 先檢查 `pci_enable_device()` 與 BAR index
