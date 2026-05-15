# Debug Checklist

這一關必須在 Linux guest 內驗證，且 guest 必須看得到 QEMU EDU。

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
