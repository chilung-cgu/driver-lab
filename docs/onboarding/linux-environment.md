# Linux / QEMU 環境：先證明實驗位置正確

> **定位**：這份文件集中處理 Linux host、QEMU guest、matching kernel build tree、debugfs、module policy 與 `check-kernel-env.sh`。環境 gate 未成立時，不要先改 driver source。

## 先講結論

Kernel module 必須針對「實際要載入它的 running Linux kernel」建置。macOS 可當 editor 或 QEMU host，但不能載入 Linux `.ko`。Labs05～07 還需要 Linux guest 內能列舉 QEMU EDU `1234:11e8`。

```text
macOS / Linux host
  └─ QEMU process、guest image、network/storage
       └─ Linux guest
            ├─ running kernel / matching build tree
            ├─ lspci 看見 EDU
            └─ build/load/test Labs05～07
```

Cross-architecture（例如 ARM host 跑 x86_64 guest）通常用 TCG；不要假設 KVM/HVF 能跨 ISA 加速。

## 不確定處與驗證狀態

- Distro package name、Secure Boot policy、module signing 與 kernel config 依環境而異。
- `check-kernel-env.sh` 只做 prerequisite inspection，不測 driver behavior。
- QEMU BDF、IRQ number 與 acceleration availability 不應 hard-code。
- Real hardware 還有 firmware、IOMMU、slot/hotplug 與 platform policy。

## 第一個 gate

```sh
uname -s
uname -m
uname -r
test -e "/lib/modules/$(uname -r)/build"
command -v make
command -v gcc
command -v git
./scripts/check-kernel-env.sh
```

### 輸出怎麼讀

| 輸出 | 回答的問題 | 不代表什麼 |
|---|---|---|
| Kernel / `uname -r` | module target kernel 是誰 | headers 一定 matching |
| Build tree | external module 是否有 kbuild 入口 | module 一定能 load |
| make/gcc/git | 基本工具是否存在 | toolchain/config 完整 |
| debugfs mounted | Lab01 observation surface 可用 | debugfs entry 已建立 |
| Secure Boot state | unsigned module 是否可能被 policy 擋 | load failure 一定由它造成 |
| taint | kernel 是否已有值得記錄的 taint flags | taint=0 代表 driver 正確 |

### Matching build tree

External module 的典型建法：

```sh
make -C "/lib/modules/$(uname -r)/build" M="$PWD"
```

`KDIR` 只是常見變數名；它通常指向 running kernel 的 build tree。只有任意一份 kernel headers 不夠。

## Debugfs

檢查：

```sh
grep ' /sys/kernel/debug ' /proc/mounts
```

需要時：

```sh
./scripts/mount-debugfs.sh
```

Debugfs 是開發觀測介面，不是 stable product UAPI。它未掛載只會讓你看不到 surface，不等於 module init 一定失敗。

## Secure Boot、signature 與 lockdown

```sh
mokutil --sb-state 2>/dev/null || true
sudo dmesg | tail -n 100
```

若 `insmod` 被拒絕，依序查：

1. running kernel/architecture/build tree；
2. `modinfo module.ko` 的 vermagic/architecture/license；
3. kernel loader log；
4. signature、Secure Boot/lockdown；
5. dependency、symbol/version 與 module policy。

不要用 `rmmod -f` 或關閉安全機制來掩蓋 lifecycle bug。

## QEMU EDU gate

在 guest 內：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

看不到 EDU 時，問題早於 driver binding/probe：先查 QEMU command line、machine/device model、guest PCI enumeration，不要先改 `probe()`。

進一步文件：

- [`../../qemu/README.md`](../../qemu/README.md)
- [`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)
- [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
- [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)

## 常用命令

```sh
# Build/load/unload
make
modinfo ./module.ko
sudo insmod ./module.ko
lsmod | grep module_name
sudo rmmod module_name

# Kernel evidence
sudo dmesg | tail -n 100
journalctl -k -n 100

# PCI evidence
lspci -Dnn
lspci -Dnnk -d 1234:11e8
```

## 不建議的環境捷徑

- 不在 macOS 直接載入 Linux `.ko`。
- 不把 Docker container 當 PCI/kernel-module runtime environment；container 分享 host kernel，通常也看不到你需要的 PCI/QEMU hierarchy。
- 不因 compile pass 就跳過 matching runtime kernel。
- 不把固定 BDF、IRQ number、page size 或 QEMU accel 寫成通則。

## Self-check

1. 為什麼 module 要對 running kernel build tree 編譯？
2. `check-kernel-env.sh` pass 能證明什麼、不能證明什麼？
3. Mac ARM host 跑 x86_64 guest 為什麼通常使用 TCG？
4. Guest `lspci` 看不到 EDU 時，問題位於 probe 之前還是之後？
5. `Invalid module format` 的第一輪排查順序是什麼？

<details>
<summary>參考答案</summary>

1. External module 依賴 target kernel headers/config/symbol/version/architecture；任意 headers 可能產生不相容 vermagic 或 ABI。
2. 只證明基本環境與風險檢查完成；不證明 source、load、callback、MMIO/IRQ/DMA 行為正確。
3. KVM/HVF 通常只加速相同 host/guest ISA；跨 ISA 需 QEMU software translation/emulation。
4. 之前。沒有被 PCI core 枚舉的 `pci_dev`，driver core 就沒有 match/bind/probe target。
5. running kernel/arch/build tree → modinfo/vermagic → loader dmesg → signature/lockdown → dependency/symbol policy。

</details>

## 來源與查證

- External modules: <https://docs.kernel.org/kbuild/modules.html>
- Module signing: <https://docs.kernel.org/admin-guide/module-signing.html>
- Debugfs: <https://docs.kernel.org/filesystems/debugfs.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
