# Debug Checklist

這份清單用來排查 `ioctl`、`poll`、`mmap` 三種新路徑。

## 症狀：`ioctl` 回失敗

先查證據：

```sh
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
sudo dmesg | tail -n 50
```

常見原因：

- CLI 和 kernel module 使用的 UAPI header 不一致
- `cmd` number 不正確
- `copy_from_user()` / `copy_to_user()` 失敗

## 症狀：`poll` 沒有醒

先查證據：

```sh
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 poll 3000
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 trigger
```

常見原因：

- driver 沒有 `wake_up_interruptible()`
- `poll_wait()` 掛錯 waitqueue
- 事件旗標與 buffer 狀態沒有同步更新

## 症狀：`mmap-read` 內容不對

先查證據：

```sh
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 mmap-read
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
```

常見原因：

- shared page 沒有在 state 改變後同步
- `mmap` size 或 offset 不符合 driver 預期
- userspace 讀到的是舊狀態，不是剛更新後的快照
