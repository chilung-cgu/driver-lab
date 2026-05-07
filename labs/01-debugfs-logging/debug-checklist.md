# Debug Checklist

- `debugfs` 是否已掛載？
- `status` 是否可讀？
- `trigger_count` 是否有增加？
- `dmesg` 是否看到 `trigger #...`？
- `/proc/dynamic_debug/control` 是否存在？
- dynamic debug 開啟後，是否能看到 `pr_debug()` 訊息？

