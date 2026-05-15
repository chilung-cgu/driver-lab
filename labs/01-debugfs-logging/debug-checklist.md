# Debug Checklist

這份清單用來排查 `debugfs`、`trigger`、dynamic debug 相關問題。

## 症狀：`/sys/kernel/debug/driver_lab_debugfs` 不存在

先查證據：

```sh
mount | grep debugfs
ls /sys/kernel/debug
sudo dmesg | tail -n 50
```

常見原因：

- `debugfs` 尚未掛載
- module 沒有成功 `insmod`
- init path 建立 debugfs entry 時失敗

## 症狀：寫 `trigger` 後狀態沒變

先查證據：

```sh
cat /sys/kernel/debug/driver_lab_debugfs/status
cat /sys/kernel/debug/driver_lab_debugfs/trigger_count
sudo dmesg | tail -n 50
```

常見原因：

- 寫入的不是正確路徑
- 權限不足，`tee` 沒有透過 `sudo`
- `copy_from_user()` 或 payload 長度處理失敗

## 症狀：看不到 `pr_debug()`

先查證據：

```sh
test -e /proc/dynamic_debug/control && echo yes
grep driver_lab_debugfs_logging /proc/dynamic_debug/control
```

常見原因：

- kernel 沒有 dynamic debug 支援
- 尚未對 module 打開 `+p`
- 只看 `pr_info()`，誤以為 `pr_debug()` 會預設顯示
