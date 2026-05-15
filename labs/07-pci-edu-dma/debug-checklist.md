# Debug Checklist

這一關必須在 Linux guest 內驗證。先確認 `05` 與 `06` 都穩定。

## 症狀：DMA mask 設定失敗

先查證據：

```sh
sudo dmesg | tail -n 80
lspci -vv -s <edu-bdf>
```

常見原因：

- 裝置可定址範圍和 driver 設定不一致
- QEMU EDU 的 DMA mask 限制沒有被正確處理
- PCI device 尚未被正確 enable

## 症狀：DMA timeout

先查證據：

```sh
sudo dmesg | tail -n 120
cat /proc/interrupts | grep driver_lab_edu_dma
```

常見原因：

- DMA command bit 沒有清掉
- IRQ completion 沒有發生
- source / destination / count register 設定錯誤

## 症狀：round-trip compare failed

先查證據：

```sh
sudo dmesg | tail -n 120
```

常見原因：

- RAM -> EDU 或 EDU -> RAM 方向搞反
- 傳輸長度不一致
- coherent buffer 切成 tx/rx 兩半時位址計算錯誤
