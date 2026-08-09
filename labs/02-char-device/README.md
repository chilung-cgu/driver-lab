# 02 — Character device、VFS read/write 與 record semantics

> **定位**：Lab02 把 `/dev`、cdev 與 `file_operations.read/write` 串起來。Current implementation 是一個 global text-like record；每次 open 有自己的 file/f_pos，但它不是 multi-client queue，也不是 production byte stream。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab02 把 `/dev`、cdev 與 `file_operations.read/write` 串起來。Current implementation 是一個 global text-like record；每次 open 有自己的 file/f_pos，但它不是 multi-client queue，也不是 production byte stream。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current source/test 已覆蓋 dev_t、cdev/class/device lifetime、usercopy、read/write 與 filesystem surfaces；多 reader/queue semantics 不在本 lab 保證內。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

初學者容易把 `/dev/foo` 當普通檔案，或以為 `write()` 進 kernel 後一定完整成功。另一個常見誤解是 global buffer 加每個 open 的 f_pos 就自然成為可靠多 client stream。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **dev_t** | major/minor 組合的 device number | 不是 file descriptor |
| **cdev** | 把 dev_t 與 file_operations 註冊給 VFS | 不自動建立 `/dev` node |
| **file_operations** | VFS dispatch 到 driver callback 的 operation table | 不是 userspace callback |
| **f_pos** | 每個 open file description 的 offset | 不等於 global buffer owner |

## 心智模型

把 char device 想成一個共用公告欄：`/dev` 是入口，每次 open 是自己的閱讀狀態，但公告內容仍可能全域共用。要做 queue，還需 record ownership、per-reader cursor 與 backpressure。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
alloc dev_t
→ cdev_add
→ class_create/device_create 形成 sysfs + /dev surface
→ open/read/write callbacks validate and usercopy
→ device_destroy/class_destroy/cdev_del/unregister
```

## 從簡單到精確

### Current source map

- `driver_lab_char.c`：registration、global buffer、`dl_read()`/`dl_write()`。
- `test.sh`：/dev、sysfs、/proc/devices、read/write/error/unload。
- `runtime/include/driver_lab_uapi.h`：後續共用的 fixed-width UAPI types。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```text
userspace write
→ validate count
→ copy_from_user before publishing state
→ synchronized update
→ read rechecks availability and copy_to_user
→ return actual bytes or negative errno
```

## 看似合理但錯誤的寫法

錯誤做法：把 userspace pointer 直接 dereference，或 copy_to_user 失敗後仍清掉 global record。這會造成 fault、安全問題或資料被無聲丟失。

## 如何執行與觀察

```sh
cd labs/02-char-device
./test.sh
```

手動觀察 `/dev/driver_lab_char0`、`/sys/class/.../dev`、`/proc/devices`，再使用 `printf`/`cat` 或 current CLI 進行 read/write。

### 能證明／不能證明

能證明 registration、node/surface、一次 read/write 與 cleanup。不能由此推論多 reader 公平、message queue、partial-record policy、poll/mmap 或 stable production ABI。

## Debug order

1. 先確認 module init 與 dev_t/cdev registration。
2. 確認 class/device_create 是否成功及 udev/node permission。
3. 分開查 callback 是否進入、count/offset policy、usercopy return。
4. 檢查 global buffer 與 per-open f_pos 是否符合預期 model。
5. 卸載後確認 /dev/sysfs/proc surfaces 都消失。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `VFS/cdev` | syscall 到 callback dispatch | message queue semantics |
| `copy_to/from_user` | 受檢查跨 boundary copy | 不保證不 fault/sleep |
| `mutex` | global record mutual exclusion | per-client ownership/ordering 的全部設計 |
| `sysfs/proc/dev` | registration evidence | callback correctness proof |

## 與 pcie-study 的對應

PCIe driver 的 control/data UAPI 也會經 VFS 或 subsystem interface；先學會 callback、bytes/errno、per-open/global state。對應 `pcie-study` P1-06、P1-14、P3-03。

## 常見誤解

### 誤解：`/dev` 是磁碟檔案

它是 VFS 入口，operation 由 driver callbacks 定義。

### 誤解：write 成功就一定寫完整

syscall 可回 partial bytes；本 lab 的 policy 要明確查看。

### 誤解：global buffer 是 queue

沒有 record ownership、capacity、cursor、公平與 backpressure。

## 適用邊界與尚未驗證

- 本 lab 是全域單筆 record 教學模型。
- 沒有 poll/ioctl/mmap；這些在 Lab03 加入。
- 產品 UAPI 還需 versioning、permissions、compat、安全與 hot-unplug lifetime。

## 第一次閱讀先記住

1. `/dev` 只是入口，semantics 由 callbacks 定義。
2. 回傳 bytes/errno 是 ABI 的一部分。
3. 要分開 global device state 與 per-open state。

## Self-check

1. cdev_add 與 device_create 分別建立什麼？
2. 為什麼不能直接 dereference userspace pointer？
3. global buffer 與 per-open f_pos 為何不等於 queue？
4. copy_to_user 失敗時為什麼不應先消費 record？
5. Lab02 test pass 不能證明哪些 multi-client 性質？

<details>
<summary>參考答案</summary>

1. cdev_add 註冊 dev_t 到 file_operations；device_create 建立 device-model/sysfs surface，udev 才可能建立 `/dev` node。
2. Userspace address 可能無效、fault 或變動；必須用 usercopy API 並處理未複製 bytes。
3. f_pos 屬每次 open，但 backing record 全域共享；缺少 per-reader ownership/cursor/capacity/backpressure。
4. 否則 userspace 沒收到資料，driver 卻把唯一 record 清掉，造成 silent loss。
5. 不能證明公平、無 starvation、record framing、concurrent readers/writers 或 production ABI stability。

</details>

## 來源與查證

- Character devices/VFS APIs: <https://docs.kernel.org/core-api/kernel-api.html>
- Usercopy guidance: <https://docs.kernel.org/core-api/memory-allocation.html>
- Current source: `labs/02-char-device/driver_lab_char.c`
