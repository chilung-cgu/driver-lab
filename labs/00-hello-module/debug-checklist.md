# Debug Checklist

這份清單只在 `00-hello-module` 跑不通時使用。第一次學習時先看 `README.md`。

## 症狀：`make` 失敗

先查證據：

- `uname -r`
- `/lib/modules/$(uname -r)/build` 是否存在
- 錯誤訊息是否提到 `No such file or directory` 或 kernel headers

常見原因：

- 沒安裝目前 kernel 對應的 headers / build tree
- 不是在 Linux 上 build
- guest / host 的 kernel 版本與 headers 不一致

## 症狀：`insmod` 失敗

先查證據：

```sh
sudo dmesg | tail -n 50
modinfo ./driver_lab_hello.ko
```

常見原因：

- Secure Boot / module signing 阻擋
- `repeat` 參數超出 `1-8`
- module 已經載入過，尚未 `rmmod`

## 症狀：看不到 log

先查證據：

```sh
sudo dmesg | tail -n 30
lsmod | grep '^driver_lab_hello'
```

常見原因：

- module 沒有成功載入
- 看錯 log 視窗，`pr_info()` 不是印到 terminal stdout
- `dmesg` 權限不足，需要 `sudo`
