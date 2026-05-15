# Debug Checklist

這一關必須在 Linux guest 內驗證。先確認 `05-pci-edu-mmio` 已穩定。

## 症狀：`request_irq()` 失敗

先查證據：

```sh
sudo dmesg | tail -n 80
cat /proc/interrupts | grep driver_lab_edu_irq
```

常見原因：

- IRQ vector 配置失敗
- shared IRQ flag 和實際 vector 類型不一致
- 前面的 PCI enable / BAR map 尚未成功

## 症狀：handler 沒進來

先查證據：

```sh
sudo dmesg | tail -n 80
cat /proc/interrupts
```

常見原因：

- 沒有真的寫 interrupt raise register
- status bit 不符合 handler 判斷條件
- IRQ 沒有正確註冊到目前裝置

## 症狀：中斷一直重進

先查證據：

```sh
sudo dmesg | tail -n 80
cat /proc/interrupts | grep driver_lab_edu_irq
```

常見原因：

- acknowledge register 沒有清乾淨
- handler 回傳值不正確
- status bit 判斷與 ack mask 不一致
