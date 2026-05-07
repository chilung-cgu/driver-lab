# Debug Checklist

- `make` 是否成功？
- `/lib/modules/$(uname -r)/build` 是否存在？
- `sudo insmod ./driver_lab_hello.ko` 是否成功？
- `sudo dmesg | tail -n 30` 是否看到：
  - `init`
  - `hello`
  - `exit`
- 如果 `insmod` 失敗：
  - 看 `dmesg`
  - 檢查 Secure Boot / module signature

