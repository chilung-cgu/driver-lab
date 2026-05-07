# Debug Checklist

- `/dev/driver_lab_char0` 是否存在？
- `sudo dmesg | tail -n 50` 是否看到：
  - `device opened`
  - `wrote ... bytes`
  - `read ... bytes`
  - `device released`
- 如果 `device_create()` 失敗：
  - 看 `dmesg`
  - 確認 `class_create()` 是否成功
- 如果 userspace read/write 失敗：
  - 檢查 return value
  - 檢查 buffer size
  - 檢查 `copy_to_user` / `copy_from_user` path

