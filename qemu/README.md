# QEMU Notes

這個目錄是 Lab05～Lab07 的 QEMU EDU 操作入口。

> [!NOTE]
> 完全沒做過kernel module時，先完成Lab00～Lab04第一輪。QEMU環境不是用來取代module、VFS與concurrency基本功。

## 這個目錄負責什麼

- 提供QEMU EDU的host啟動入口；
- 說明host／guest與CPU architecture邊界；
- 提供guest bring-up與Lab05～07檢查表；
- 指向current source與smoke tests。

## 不負責什麼

- macOS不能直接build/load Linux kernel module；
- Container不能取代一台能列舉QEMU EDU的Linux guest；
- Generic QEMU流程不保證適用每個distro/image/network設定；
- 通過EDU不代表真實card的PHY、firmware、reset與hotplug已驗證。

## 支援的執行模型

- Linux host + Linux guest；
- macOS host + Linux guest；
- x86_64 host + x86_64 guest（常可用KVM）；
- arm64 host + arm64 guest（平台支援時可用KVM/HVF）；
- arm64 host + x86_64 guest（通常使用TCG軟體翻譯）。

真正的driver build/load/smoke test位置是**能看見EDU且kernel headers匹配的Linux guest**。

> [!WARNING]
> 本accuracy-audit branch已通過static checks與external-module compile gate，但尚未在你的Linux/QEMU guest完成MMIO、IRQ、DMA runtime驗證。不要把舊文件中的「已實測通過」當成這個branch的證據；合併前應保存本次test logs。

## 為什麼使用QEMU EDU

QEMU官方提供`edu`作driver教育裝置，包含：

- PCI configuration/BAR0；
- MMIO identification/liveness與其他register；
- interrupt；
- 簡化DMA engine與device-local RAM。

它適合建立第一個host-driver閉環，但不是production PCIe accelerator模型。

## 你要完成的gate

1. Host有正確architecture的QEMU system emulator。
2. Launch arguments包含`-device edu`。
3. Guest內：

   ```sh
   uname -m
   uname -r
   lspci -Dnn | grep 1234:11e8
   test -e "/lib/modules/$(uname -r)/build"
   ```

4. 依序跑Lab05、Lab06、Lab07。
5. 保存`dmesg`、`lspci -Dnnvv`與`/proc/interrupts`證據。

## 文件地圖

- [`launch-edu-vm.sh`](launch-edu-vm.sh)：QEMU啟動腳本。
- [`launch-edu-vm.sh.md`](launch-edu-vm.sh.md)：script companion；若與script不同，以script為準。
- [`edu-bringup-checklist.md`](edu-bringup-checklist.md)：host到guest的最小gate。
- [`arm-host-x86-guest.md`](arm-host-x86-guest.md)：arm64 host模擬x86_64 guest的可重建流程。
- [`../docs/concepts/pcie-primer.md`](../docs/concepts/pcie-primer.md)：PCI/BAR/MMIO/IRQ/DMA正確最低模型。
- [`../docs/guides/lab-05-study-order.md`](../docs/guides/lab-05-study-order.md)：Lab05閱讀順序。
- [`../docs/guides/qemu-edu-first-pass.md`](../docs/guides/qemu-edu-first-pass.md)：第一次白話導讀。
- [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)：完整runbook。
- [`../docs/guides/linux-guest-05-to-07-checklist.md`](../docs/guides/linux-guest-05-to-07-checklist.md)：第二次後速查。

## Accelerator選擇

`launch-edu-vm.sh`會依host與可用backend選擇；實際結果以QEMU輸出為準：

- Linux同architecture guest通常優先KVM；
- macOS相容guest通常可用HVF；
- 不支援時退回TCG；
- arm64 host跑x86_64 guest通常只能TCG。

可明確覆寫：

```sh
QEMU_IMAGE="$HOME/vm/driver-lab.qcow2" \
QEMU_ACCEL=tcg \
./qemu/launch-edu-vm.sh
```

加入`QEMU_EXTRA_ARGS`前先讀script的argument handling；複雜quoted arguments較適合寫在本機wrapper或改成明確array，而不是假設任意字串展開都安全。

## 建議閱讀順序

1. [`../docs/concepts/pcie-primer.md`](../docs/concepts/pcie-primer.md)
2. [`../docs/guides/lab-05-study-order.md`](../docs/guides/lab-05-study-order.md)
3. [`../docs/guides/qemu-edu-first-pass.md`](../docs/guides/qemu-edu-first-pass.md)
4. [`edu-bringup-checklist.md`](edu-bringup-checklist.md)
5. 跨architecture時讀[`arm-host-x86-guest.md`](arm-host-x86-guest.md)
6. [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)

## 現在不要混在一起追

- 不要在Lab05環境/bind未通時先追DMA；
- 不要把MSI-X、AER、DPC、power/reset、hot-unplug一次塞進EDU第一輪；
- 不要把QEMU的BDF寫死；
- 不要把EDU generic reset fallback當成真實硬體recovery；
- 不要以「module可以compile」宣稱IRQ/DMA runtime通過。
