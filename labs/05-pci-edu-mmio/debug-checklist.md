# Debug Checklist

這一關必須在 Linux guest 內驗證，且 guest 必須看得到 QEMU EDU。

## 症狀：`lspci` 指令不存在

先查證據：

```sh
command -v lspci
```

常見原因：

- guest 還沒安裝 `pciutils`
- 你在太精簡的 guest image 裡測試

Debian/Ubuntu guest 通常可以先補：

```sh
sudo apt update
sudo apt install -y pciutils
```

## 症狀：`lspci` 看不到 `1234:11e8`

先查證據：

```sh
lspci -nn
```

常見原因：

- QEMU 啟動時沒有加 `-device edu`
- 你在 host 不是 guest 裡查
- guest image 或 QEMU 啟動流程不是本 repo 文件描述的那台 VM

## 症狀：`probe()` 沒進來

先查證據：

```sh
sudo insmod ./driver_lab_edu_mmio.ko
sudo dmesg | tail -n 80
lspci -nn | grep 1234:11e8
```

常見原因：

- PCI ID table 不 match
- module 沒有成功載入
- 裝置已被其他 driver bind

如果 `insmod` 成功但 `probe start` 沒出現在 `dmesg`，再補查：

```sh
ls -l /sys/bus/pci/drivers/driver_lab_edu_mmio
find /sys/bus/pci/devices -maxdepth 2 -name driver -type l -ls
```

這能幫你分辨「driver 沒註冊」和「device 沒 bind 到這支 driver」。

## 症狀：BAR map 或 liveness check 失敗

先查證據：

```sh
sudo dmesg | tail -n 80
lspci -vv -s <edu-bdf>
```

常見原因：

- BAR index 寫錯
- `pci_enable_device()` 或 `pci_request_region()` 失敗
- register offset 或 liveness 預期值不對

## 症狀：`test.sh` 的 dmesg grep 失敗

先查證據：

```sh
sudo dmesg | tail -n 120
```

常見原因：

- `dmesg -C` 因權限或 kernel 設定沒有清掉舊 log，導致你正在看混雜輸出
- module 其實載入失敗，後面的 `probe start`、`BAR0 mapped`、`liveness check passed` 自然不會出現
- driver bind 到 device 前就失敗，應該回到 `probe()` 沒進來的情境排查
