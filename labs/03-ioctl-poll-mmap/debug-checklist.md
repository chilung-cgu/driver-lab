# Debug Checklist

- `ioctl` command number 是否正確？
- `copy_to_user` / `copy_from_user` 是否檢查回傳值？
- blocking 與 non-blocking 行為是否一致？
- `poll` 是否正確回報 `POLLIN` / `POLLOUT`？
- `mmap` buffer 的 lifecycle 是否清楚？

