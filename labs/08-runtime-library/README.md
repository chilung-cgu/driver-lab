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
