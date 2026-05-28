# 08 - Runtime Library

## 目標

把 driver 的 userspace 使用方式包裝成比較像真實產品的 runtime。

> [!NOTE]
> 這一關目前已驗證的是 `build`，以及對 `02/03` 的基本封裝能力。
> 它不是產品級 runtime，也還沒有把 timeout / retry / error policy 做到完整。

## 先備條件

- 你已經讀過 [`../../docs/onboarding/07-to-09-runtime-validation-bridge.md`](../../docs/onboarding/07-to-09-runtime-validation-bridge.md)
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

## 目前已驗證到哪裡

- `make -C runtime clean all` 可建出 runtime 與 CLI
- `tests/driver_lab_char_cli.c` 可連同 runtime 一起編譯
- runtime 目前已封裝 `02` 與 `03` 真正有用到的介面

## 這一關會出現哪些 filesystem 入口

`08` 是 userspace runtime，不會建立新的 kernel filesystem 入口。

| 入口 / artifact | 第一輪用途 |
|---|---|
| `runtime/include/driver_lab_runtime.h` | userspace runtime 對外 API。 |
| `runtime/include/driver_lab_uapi.h` | kernel/userspace 共享 ABI 定義。 |
| `tests/driver_lab_char_cli` | build 出來的 CLI binary。 |
| `/dev/driver_lab_char0`、`/dev/driver_lab_ctl0` | 只有在回頭操作 `02/03` driver 時才會用到。 |

所以這關 build 成功不代表 kernel driver 已載入；真正 driver 行為仍要回 `02/03` 的 smoke test 驗。

## 目前還沒驗證到哪裡

- 還沒證明它已具備產品級 timeout / retry policy
- 還沒證明它適合直接延伸到 `05-07`
- 真正的行為驗證仍依附 `02-char-device` 與 `03-ioctl-poll-mmap`

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

## 這一關現在怎麼驗

這一關目前的驗證分兩層：

1. `runtime/` 自己能不能穩定 build
2. `02/03` 的 userspace CLI 能不能透過這層 runtime 被建出來

如果你要驗「這個 runtime 呼叫實際 driver 真的正確」，還是要回到：

- [`../02-char-device/README.md`](../02-char-device/README.md)
- [`../03-ioctl-poll-mmap/README.md`](../03-ioctl-poll-mmap/README.md)

## `test.sh` 逐段在驗什麼

1. 找到 repo root 與 `tests/driver_lab_char_cli` 的輸出位置。
2. `make -C runtime` 建出 runtime 與 CLI。
3. `test -x` 確認 CLI binary 真的存在且可執行。
4. 故意不帶參數執行 CLI，確認它會印 usage 並回傳非 0。
5. grep `Usage:`，確認最小 CLI 行為符合預期。

這支 test 不載入 kernel module。真正 driver 行為仍由 `02/03` 的 Linux smoke test 驗證。

## 如果你完全看不懂 source code，先看這 5 個點

1. `runtime/include/driver_lab_runtime.h`：userspace 對外 API 長什麼樣子。
2. `runtime/include/driver_lab_uapi.h`：kernel/userspace 共享的 ABI 常數與 struct。
3. `dl_runtime_open()` / `dl_runtime_close()`：runtime 如何管理 fd。
4. `dl_runtime_ioctl_*()`：runtime 如何包 `03` 的 control path。
5. `tests/driver_lab_char_cli.c`：CLI 如何呼叫 runtime，而不是到處直接散寫 syscall。

## API 參數第一輪怎麼讀

`08` 是 userspace runtime，不是 kernel driver。這一關仍然可以用「參數角色」讀 code，但不要把 runtime helper 誤認成 kernel API。

完整模板見 [`../../docs/onboarding/kernel-api-parameter-roles.md`](../../docs/onboarding/kernel-api-parameter-roles.md)。

| API | 參數角色 | 第一輪理解 |
|---|---|---|
| `dl_runtime_open_flags(handle, path, flags)` | output handle、device path、open flags | 成功後 `handle->fd` 保存 opened device fd。 |
| `dl_runtime_write(handle, buf, count)` | handle、source buffer、byte count | userspace wrapper，最後呼叫 `write()` 進 driver `.write` callback。 |
| `dl_runtime_read(handle, buf, count)` | handle、destination buffer、capacity | userspace wrapper，最後呼叫 `read()` 從 driver 讀資料。 |
| `dl_runtime_ioctl_set_message(handle, message)` | handle、C string | runtime 把字串包成 UAPI struct，再送 `ioctl`。 |
| `dl_runtime_poll_readable(handle, timeout_ms, &revents)` | handle、timeout、output events | 等 driver fd 可讀或有 event，並把結果填回 `revents`。 |

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | runtime 是 userspace library，不是 kernel driver；它把 open/read/write/ioctl/poll/mmap 包成一致的 C API；目前主要服務 `02/03`。 |
| 可以先略過 | 產品級 timeout/retry/error mapping；跨版本 ABI compatibility；PCIe/QEMU EDU 專用 helper。 |
| 之後再回來補 | runtime API 穩定性、錯誤碼收斂、CLI/test 如何避免直接散寫 syscall。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| runtime 是 kernel driver 嗎？ | 不是。runtime 是 userspace 封裝層，負責把 driver ABI 包成較一致的 C API。 |
| runtime 包了哪些路徑？ | `open/close`、`read/write`、`ioctl`、`poll`、`mmap`。 |
| 第一個觀測點是什麼？ | `make -C runtime` 能建出 `tests/driver_lab_char_cli`，CLI 無參數時會印 usage 並回傳非 0。 |
| runtime 與 UAPI header 的關係是什麼？ | UAPI header 定義 kernel/userspace 都要同意的 ABI；runtime include 它並把 syscall 呼叫包起來。 |
| 這一關主要產生什麼 artifact？ | userspace CLI binary `tests/driver_lab_char_cli`，不是 `.ko`。 |
| cleanup 做了什麼？ | `make -C runtime clean` 刪除 CLI build artifact。 |
| runtime build 失敗時第一個看哪裡？ | 先看 compiler error，再確認 include path 是否能找到 `runtime/include/driver_lab_uapi.h`。 |

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
