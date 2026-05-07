# QEMU Notes

這個目錄保留給第 10-12 週的 `edu` PCI 裝置練習。

> [!NOTE]
> 對完全沒學過 kernel module 的新手，這個目錄現在可以先略過。
> 你目前真正要做的是 `00-02`。

## 為什麼是 QEMU EDU

QEMU 官方把 `edu` 定位成：

- 教學用 PCI 裝置
- 適合拿來寫 kernel driver
- 明確支援 `MMIO + IRQ + DMA`

## 你之後要做到的事

1. 在 Linux host 安裝 `qemu-system-x86`
2. 啟動一台 Linux guest
3. 把 `edu` 裝置掛進 guest
4. 在 guest 內 build / load 你的 PCI driver
5. 驗證：
   - BAR map
   - liveness check
   - interrupt ack
   - DMA buffer round-trip

## 這個目錄未來預期會放什麼

- `launch-edu-vm.sh`
- QEMU 啟動參數樣板
- guest image 建議
- `edu` driver bring-up checklist

## 目前已提供

- [`launch-edu-vm.sh`](launch-edu-vm.sh)：最小可用的 QEMU 啟動腳本
- [`edu-bringup-checklist.md`](edu-bringup-checklist.md)：guest 內 bring-up 清單
- [`../docs/qemu-edu-first-pass.md`](../docs/qemu-edu-first-pass.md)：第一次做 `05-07` 的白話導讀

## 目前不先做的事

- 不在 macOS 本機直接嘗試 build Linux kernel module
- 不用 Docker 代替 QEMU PCI 裝置練習
