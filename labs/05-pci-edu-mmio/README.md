# 05 - PCI EDU MMIO

## 目標

使用 QEMU `edu` 裝置完成第一個真正的 PCI driver 起手式。

> [!NOTE]
> 如果你現在還不熟 kernel module、debugfs、char device，先不要跳這一關。
> 這一關是前面基礎都站穩後，才開始接近 PCIe host driver 的起點。

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

## 參考

- [`../../qemu/README.md`](../../qemu/README.md)
- [`../../docs/source-index.md`](../../docs/source-index.md)

## 新手先記住這一關在補什麼

- 前面你都在練「沒有真硬體時的共通 driver 技能」
- 這一關開始，你才第一次真的碰到 PCI device discovery 與 MMIO register access
