# Debug Checklist

這一關目前主要驗證 runtime build 與 CLI 基本可執行性。

## 症狀：runtime build 失敗

先查證據：

```sh
make -C ../../runtime clean all
```

常見原因：

- C compiler 不存在
- include path 沒有指到 `runtime/include`
- UAPI header 與 runtime source 不一致

## 症狀：CLI 無法執行

先查證據：

```sh
../../tests/driver_lab_char_cli
```

常見原因：

- CLI 尚未 build
- 執行的是舊 artifact
- 使用方式不符合 usage

## 症狀：runtime 行為不符合 driver

先查證據：

- 回到 `02-char-device` 或 `03-ioctl-poll-mmap` 的 README 重跑實際 driver test
- 比對 runtime API 與 UAPI header

常見原因：

- runtime 封裝和 driver ABI 語意不一致
- timeout / retry / error policy 尚未產品化
