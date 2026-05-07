# 08 - Runtime Library

## 目標

把 driver 的 userspace 使用方式包裝成比較像真實產品的 runtime。

> [!NOTE]
> 這一關已經有第一版落地，不再只是純 scaffold。
> 目前 runtime 已能覆蓋 `02` 與 `03` 的基本互動，但還不是產品級 runtime。

## 先備條件

- 你已經看過 `runtime/README.md`
- 你知道前面 labs 已開始出現 userspace CLI 與 driver ABI 的配合

## 這一關要練什麼

- 封裝 open / close / read / write / ioctl / poll / mmap
- 定義 timeout / retry / error mapping
- 撰寫 CLI 或最小 sample app

## 參考起點

- [`../../runtime/README.md`](../../runtime/README.md)
- [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c)

## 成功標準

- 不直接把原始 syscall 散落在每個測試程式裡
- 有一份清楚的 runtime API header
- README 能說明 control path 與 data path

## 新手先記住這一關在補什麼

- 真實產品不會讓每個 app 都自己亂 call syscall
- 通常會有一層 runtime library，統一封裝 ABI 與 error handling

## 目前已完成的部分

- `open / close`
- `read / write`
- `ioctl set/get/trigger/clear`
- `poll`
- `mmap`
- 一個共用 CLI 測試入口

## 目前還沒完成的部分

- 更完整的 timeout / retry policy
- 更細的 error mapping
- 針對 PCIe/QEMU EDU driver 的專用 helper

## 新手現在最該理解的點

先不要把 runtime 想成「多餘的一層」。

你可以先把它想成：

- driver 定義 ABI
- runtime 把 ABI 包成比較好用的函式
- CLI / app 不用每次自己處理原始 syscall 細節

## 第一次理想上要看到的成果

第一次不需要寫出很大的 library。

只要做到下面三件事就夠了：

1. 有一份清楚的 public header
2. 有一份把 syscall 包起來的 `.c`
3. 測試程式可以透過 runtime 呼叫 driver，而不是自己直接散寫 `ioctl`

## 第一次卡住先看哪裡

- 不知道 runtime 該包哪些 API
  - 先只包現在 lab 真正有用到的介面
- 不知道 error handling 該做到多細
  - 第一版先回傳 `-errno` 或明確錯誤碼即可
- 覺得 CLI 直接 call syscall 比較快
  - 那是短期快，長期會讓 ABI 使用點四散難維護
