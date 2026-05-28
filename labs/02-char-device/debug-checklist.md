# Debug Checklist

這份清單用來排查 `/dev/driver_lab_char0`、`read/write`、cleanup 問題。

## 症狀：`/dev/driver_lab_char0` 沒出現

先查證據：

```sh
lsmod | grep '^driver_lab_char'
ls -l /dev/driver_lab_char0
ls -l /sys/class/driver_lab_char/driver_lab_char0
cat /sys/class/driver_lab_char/driver_lab_char0/dev
grep driver_lab_char /proc/devices
sudo dmesg | tail -n 50
```

常見原因：

- `insmod` 失敗
- `class_create()` 或 `device_create()` 失敗
- udev/devtmpfs 沒有建立 device node

判斷方式：

- `/sys/class/driver_lab_char/driver_lab_char0` 不存在：先追 driver init path。
- `/sys/class/driver_lab_char/driver_lab_char0` 存在但 `/dev/driver_lab_char0` 不存在：再追 devtmpfs / udev。
- `cat .../dev` 有 `major:minor`：表示 device model 已經有對應的 device number。

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
