# 08 — Userspace runtime、CLI 與 UAPI ownership

> **定位**：Lab08 不是新的 `.ko`，而是把 Lab02/03 的 raw syscalls 封裝成 userspace runtime/CLI。正確性重點是 fd/mapping ownership、partial I/O、errno、poll error bits、mmap size/version 與 close/unmap exactly once。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab08 不是新的 `.ko`，而是把 Lab02/03 的 raw syscalls 封裝成 userspace runtime/CLI。正確性重點是 fd/mapping ownership、partial I/O、errno、poll error bits、mmap size/version 與 close/unmap exactly once。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current runtime、unit test 與 CLI 可在 CI 以 `-Wall -Wextra -Werror` build；實際 device smoke 仍需要先載入 Lab02/03 module。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

Wrapper 很容易把 kernel 錯誤隱藏掉：例如假設 read/write 一次完整、close 失敗後重試舊 fd、poll 只看 POLLIN、或固定 mmap 4096。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **runtime** | application 與 raw UAPI 之間的 userspace helper layer | 不改變 kernel ABI |
| **handle ownership** | 哪個 object 負責 close/unmap | 複製 struct 不會自動轉移 |
| **partial I/O** | 成功處理少於 request bytes | 不必然是 fatal error |
| **errno** | userspace failure 原因的 thread-local convention | 不是 kernel internal stack trace |

## 心智模型

把 runtime 想成翻譯器兼資源管理員：它把 raw ioctl/read/poll/mmap 包成一致 API，但不能改寫 kernel 的 bytes/errno/ownership contract。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
initialize invalid handle
→ open exactly once
→ wrapper validates arguments and preserves bytes/errno
→ poll/map/read snapshot with returned sizes
→ unmap/close exactly once and invalidate ownership
```

## 從簡單到精確

### Current source map

- `runtime/include/driver_lab_runtime.h`：public userspace API/handle。
- `runtime/src/driver_lab_runtime.c`：open/close/read/write/ioctl/poll/mmap/snapshot wrappers。
- `tests/driver_lab_runtime_unit.c`：invalid argument、ownership 與 return convention。
- `tests/driver_lab_char_cli.c`：可操作 Lab02/03 的 CLI。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```text
handle.fd = -1
→ open publishes valid fd only on success
→ close first invalidates ownership, then calls close(fd)
→ caller never retries a stale/reused descriptor number
```

## 看似合理但錯誤的寫法

錯誤做法：把一個已 open 的 handle 複製兩份，兩邊都 close；或 close 回錯後用同一舊整數重試，可能關到已被其他 thread 重用的新 fd。

## 如何執行與觀察

```sh
make -C runtime clean all
make -C runtime test
```

要做 device smoke，再先載入 Lab02/03，使用 `tests/driver_lab_char_cli` 執行 write/read/ioctl/poll/mmap-read。

### 能證明／不能證明

Compile/unit test 能證明 argument/ownership 的一部分 userspace contract；只有連到 current module 的 smoke 才能證明 UAPI integration。兩者都不代表 production thread safety。

## Debug order

1. 先區分 wrapper input validation、syscall errno、partial bytes 與 kernel log。
2. 檢查 handle 是否仍 owner，以及 fd 是否已 invalidate。
3. Poll 同時看 return count 與 POLLERR/POLLHUP/POLLNVAL。
4. Mmap size 取自 ioctl/status 與 sysconf page size，不硬編碼。
5. Snapshot retry 問題保存 begin/end seq 與 errno。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `wrapper` | 一致 argument/ownership/error handling | 修正 kernel bug |
| `unit test` | 無 device 的 userspace invariants | UAPI runtime integration |
| `CLI smoke` | 指定 module/UAPI 正常路徑 | concurrent/hostile workload proof |
| `errno + bytes` | 精確回報 outcome | 自動重試 policy |

## 與 pcie-study 的對應

Accelerator driver 通常需要 userspace runtime 管理 queues、mappings、events 與 handles；這一關先把 ownership/error semantics 做對。對應 `pcie-study` P1-14、P3-09。

## 常見誤解

### 誤解：Wrapper 可以忽略 partial I/O

它必須保留或明確完成 retry policy。

### 誤解：close 失敗就重試同一 fd

Linux 可能已釋放 descriptor number；盲目重試危險。

### 誤解：固定 4096 可跨平台

page size 與 UAPI mapping size需 runtime取得。

## 適用邊界與尚未驗證

- Runtime 目前不是 thread-safe ownership framework。
- 沒有 ABI negotiation、async queues、pinned memory 或 VFIO/IOMMUFD。
- Unit test 不載入 kernel module；integration evidence 必須分開記錄。

## 第一次閱讀先記住

1. Userspace wrapper 也有 resource lifetime。
2. 保留 bytes/errno，不用 convenience 隱藏 contract。
3. Unit、compile、device smoke 是不同證據層。

## Self-check

1. 為什麼 open handle 不應直接複製？
2. Close 前先把 fd 設為 -1 的理由是什麼？
3. Partial I/O 應如何處理？
4. Poll 為什麼不能只看 ret > 0？
5. Unit test pass 能否證明 kernel UAPI integration？

<details>
<summary>參考答案</summary>

1. 複製只複製 descriptor number，沒有複製 ownership；兩份 handle 可能 double close。
2. 即使 close 回錯，Linux 可能已釋放 descriptor；先 invalidate 避免危險重試或 double close。
3. 保留實際 bytes 給 caller，或由明確 retry loop 完成；不能默認等於 request count。
4. 還要檢查 revents 中的 POLLERR/POLLHUP/POLLNVAL 與實際 readiness bits。
5. 不能；unit test 只覆蓋 userspace logic，需 current module 的 smoke/runtime evidence。

</details>

## 來源與查證

- open/close/read/write/poll/mmap: <https://man7.org/linux/man-pages/>
- Userspace API guidance: <https://docs.kernel.org/userspace-api/index.html>
- Current source: `runtime/src/driver_lab_runtime.c`
