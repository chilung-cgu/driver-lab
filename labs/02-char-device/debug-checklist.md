# Debug Checklist

這份清單用來排查 `/dev/driver_lab_char0`、`read/write`、cleanup 問題。

## 症狀：`/dev/driver_lab_char0` 沒出現

先查證據：

```sh
lsmod | grep '^driver_lab_char'
ls -l /dev/driver_lab_char0
sudo dmesg | tail -n 50
```

常見原因：

- `insmod` 失敗
- `class_create()` 或 `device_create()` 失敗
- udev/devtmpfs 沒有建立 device node

## 症狀：write 或 read 失敗

先查證據：

```sh
printf '%s' hello | sudo tee /dev/driver_lab_char0 >/dev/null
sudo dd if=/dev/driver_lab_char0 bs=1 count=5 status=none
sudo dmesg | tail -n 50
```

常見原因：

- 寫入超過 buffer 大小
- 權限不足
- userspace buffer 與 kernel buffer 複製路徑失敗

## 症狀：卸載後仍看到殘留狀態

先查證據：

```sh
lsmod | grep '^driver_lab_char'
ls -l /dev/driver_lab_char0
```

常見原因：

- `rmmod` 沒有成功
- 有 process 還開著 device node
- cleanup path 沒有和 init path 對稱
