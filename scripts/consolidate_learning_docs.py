#!/usr/bin/env python3
"""Consolidate overlapping learner-facing documentation into canonical files.

This is a one-shot pedagogy migration. It writes the new canonical documents,
removes superseded entry/bridge/reference files, and rewrites Markdown links
throughout the repository so local-link validation can prove the new graph is
self-consistent.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

START_HERE = r'''# START HERE — 從 0 基礎走完 Linux host-driver labs

> **定位**：這是 `driver-lab` 唯一的新手總入口。第一次進 repo、忘記下一步、或覺得文件太多時，只回到這一頁。

## 先講結論

這個 repo 刻意把 Linux host driver 拆成十個可驗證的小閉環：

```text
module lifecycle
→ debugfs / logging
→ char-device read/write
→ ioctl / poll / mmap
→ concurrency / lifetime
→ PCI BAR / MMIO
→ IRQ
→ DMA
→ userspace runtime
→ stress / fault-injection scaffold
```

你不需要先把 kernel、PCIe 或所有 `.md` 看完。每一關都只走：

```text
讀該 Lab README 的結論與名詞
→ 畫 resource / data flow
→ 讀 current source 的入口、正常路徑、error path、teardown
→ 跑 test
→ 保存 evidence
→ 闔上 README 回答 Self-check
```

## 不確定處與驗證狀態

- `review/accuracy-audit-2026-08` 是技術正確性基線。
- `review/pedagogy-pass-2026-08` 在其上改善教學結構與文件導航。
- CI 的 static、link、build、external-module compile 不等於 module runtime proof。
- Labs05～07 的 MMIO、IRQ、DMA 仍需能看見 QEMU EDU 的 Linux guest 實跑。
- QEMU EDU 只教 Linux PCI software model，不取代真實卡的 datasheet、firmware、reset、AER、PM 與 PHY/link 驗證。

## 最小心智模型

```mermaid
flowchart LR
    U["userspace\nshell / CLI / test"] --> S["syscall\nread / write / ioctl / mmap"]
    S --> V["VFS / subsystem"]
    V --> D["driver callback / PCI probe"]
    D --> K["kernel state\nlocks / waiters / mappings"]
    D --> M["BAR / MMIO"]
    D --> I["IRQ"]
    D <--> X["DMA memory"]
```

第一輪只要知道：userspace 或 kernel core 會呼叫 driver 入口；driver 取得 resource、處理 state、與 device 互動，最後必須在 teardown 前先停止所有仍會使用 resource 的執行者。

## 打開一個 Lab 目錄時，先看什麼

| 檔案 | 角色 | 第一輪閱讀深度 |
|---|---|---|
| `README.md` | 主教材、命令、evidence、limits | 必讀 |
| `driver_*.c` / runtime `.c` | current implementation | 找入口、shared state、resource、error/teardown |
| `test.sh` | smoke/regression oracle | 看它實際驗什麼、如何失敗 |
| `debug-checklist.md` | 該 Lab 的故障排查 | 失敗時再看 |
| `Makefile` | kbuild 或 userspace build glue | 知道輸入、target、KDIR 即可 |
| `*_uapi.h` | kernel/userspace ABI | Lab03/04 起需要仔細讀 |
| `<source>.md` | generated/line-by-line companion | source 讀不懂時旁讀，不當 authority |

不要用普通 `gcc driver.c` 編 kernel module；external module 交給 kbuild。不要把 `quality.sh` 當 module runtime test。

## 十關學習路線與前進 gate

| Lab | 核心問題 | 最小 evidence | 前進前要能說明 |
|---|---|---|---|
| 00 | module 如何 build/load/unload | init/exit、parameter、invalid load | failed init 為何自己 unwind |
| 01 | 如何安全導出 debug state | debugfs trigger/status、controlled log | debugfs 為何不是 stable UAPI |
| 02 | `/dev` 如何進 read/write callback | node/sysfs/proc、read/write | global record 與 per-open state 差別 |
| 03 | data/control/event/mapping 如何分工 | ioctl/poll/two-reader/read-only mmap | wakeup 為何只要求 recheck predicate |
| 04 | shared state 如何避免 lost update/UAF | unsafe vs safe、worker stop | mutex、READ_ONCE、kthread_stop 各解什麼 |
| 05 | PCI function 如何 bind、claim/map BAR | EDU enumeration、ident/liveness | BAR raw/resource/`__iomem` 差別 |
| 06 | device event 如何成為 IRQ callback | vector、status、ACK、bounded wait | source、transport、Linux IRQ、handler 差別 |
| 07 | device 如何使用 host memory | truthful mask、兩次 transfer、compare | CPU pointer、DMA address、EDU-local address 差別 |
| 08 | application 如何安全消費 UAPI | runtime/unit/CLI build、device smoke | fd/mapping ownership、partial I/O |
| 09 | 如何讓 bug 修正可重現 | reload/parallel oracle | stress、sanitizer、fault injection 的證據邊界 |

### 第一段：Labs00～02

目標是 module lifecycle、觀測、VFS/cdev/usercopy。不要急著進 QEMU。

### 第二段：Labs03～04

目標是 ABI、blocking/event、mmap snapshot、concurrency 與 lifetime。進 PCI 前至少能畫出哪些 execution paths 共享 state。

### 第三段：Labs05～07

先讀 [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)，再依序做 MMIO → IRQ → DMA。環境與操作見 [`linux-environment.md`](linux-environment.md) 與 [`../guides/lab-05-study-order.md`](../guides/lab-05-study-order.md)。

### 第四段：Labs08～09

把 raw UAPI 變成 userspace runtime，並將正常路徑、error path、reload、parallel access 變成 regression evidence。

## 會一直出現的名詞

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| resource | driver 取得並必須管理 lifetime 的物件，例如 dev_t、page、BAR、IRQ、DMA mapping | 只指 memory |
| execution context | 某段 code 執行時的規則，例如 task、hard IRQ、worker | process/thread 的單一分類 |
| callback | kernel/VFS/IRQ core 在事件發生時呼叫的 function | userspace 直接 call kernel address |
| ownership | 某段時間誰可修改、消費或釋放 resource/data | 只有 C pointer owner |
| ordering | 不同 access 對 observer 可見的先後 | operation 已完成 |
| completion | 依 protocol 某個事件/工作已完成 | payload 一定正確 |
| quiesce | 拒絕新工作、停止 producer、等待 in-flight users 退出 | 只設一個 bool |
| UAPI | userspace 可依賴的 ABI/data layout | 可隨 internal struct 任意改變 |
| BAR / MMIO | device resource window 與其 register access path | ordinary cached RAM |
| DMA address | DMA API 回傳、寫給 device 的 address | CPU virtual pointer |

更多 filesystem/API 角色見 [`kernel-interfaces.md`](kernel-interfaces.md)。

## 每個 API 固定問六件事

1. 誰呼叫它？
2. 目前 execution context 可不可以 sleep？
3. 哪些參數是 input、output、前一步 resource、數量、名字或 callback table？
4. 成功後哪個 resource/state 開始 live？
5. 哪些其他 path 可能同時碰它？
6. Failure/remove 前要先停哪個 producer 或 in-flight user？

## 三輪讀法

### 第一輪：建立圖像

讀 README 的結論、問題、名詞、心智模型與最小流程。能畫圖就好，不追所有 API 細節。

### 第二輪：理解 contract

讀 current source、正反例、resource lifetime、error unwind、test evidence 與 limits。用 function/symbol 定位，不背固定行號。

### 第三輪：建立證據

執行 test，保存 kernel/QEMU/repo SHA、command、stdout/stderr/dmesg；再修改一個條件或觸發一個 failure path，寫 bug diary。

## Evidence 分級

| 層級 | 能證明 | 不能證明 |
|---|---|---|
| 文件/source review | contract 與 implementation 意圖已檢查 | module 可執行 |
| static/compile | 語法、連結、指定 headers 下可編譯 | MMIO/IRQ/DMA runtime 正確 |
| smoke runtime | 指定環境正常路徑可重現 | race-free、所有 error path |
| stress/sanitizer | 特定 bug class 的證據增加 | 完全無 bug |
| fault injection | 指定 failure/recovery 可重現 | 真實硬體所有 failure mode |
| real hardware | 指定 board/device/firmware 的行為 | 所有平台通則 |

## 第一次執行

```sh
./scripts/check-kernel-env.sh
(cd labs/00-hello-module && ./test.sh)
```

環境輸出看不懂時讀 [`linux-environment.md`](linux-environment.md)。

## 完成一關的最低標準

- 能指出入口、shared state、live resources、成功 evidence。
- 能解釋至少一個 error/unwind 或 teardown path。
- 能說明 test 不能證明什麼。
- 能闔上 README 回答 Self-check。
- 有 exact command 和 run-specific log，而不是只留下「passed」。

## 來源與下一步

- External modules: <https://docs.kernel.org/kbuild/modules.html>
- Driver basics: <https://docs.kernel.org/driver-api/basics.html>
- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- 下一步：[`../../labs/00-hello-module/README.md`](../../labs/00-hello-module/README.md)
'''

LINUX_ENV = r'''# Linux / QEMU 環境：先證明實驗位置正確

> **定位**：這份文件集中處理 Linux host、QEMU guest、matching kernel build tree、debugfs、module policy 與 `check-kernel-env.sh`。環境 gate 未成立時，不要先改 driver source。

## 先講結論

Kernel module 必須針對「實際要載入它的 running Linux kernel」建置。macOS 可當 editor 或 QEMU host，但不能載入 Linux `.ko`。Labs05～07 還需要 Linux guest 內能列舉 QEMU EDU `1234:11e8`。

```text
macOS / Linux host
  └─ QEMU process、guest image、network/storage
       └─ Linux guest
            ├─ running kernel / matching build tree
            ├─ lspci 看見 EDU
            └─ build/load/test Labs05～07
```

Cross-architecture（例如 ARM host 跑 x86_64 guest）通常用 TCG；不要假設 KVM/HVF 能跨 ISA 加速。

## 不確定處與驗證狀態

- Distro package name、Secure Boot policy、module signing 與 kernel config 依環境而異。
- `check-kernel-env.sh` 只做 prerequisite inspection，不測 driver behavior。
- QEMU BDF、IRQ number 與 acceleration availability 不應 hard-code。
- Real hardware 還有 firmware、IOMMU、slot/hotplug 與 platform policy。

## 第一個 gate

```sh
uname -s
uname -m
uname -r
test -e "/lib/modules/$(uname -r)/build"
command -v make
command -v gcc
command -v git
./scripts/check-kernel-env.sh
```

### 輸出怎麼讀

| 輸出 | 回答的問題 | 不代表什麼 |
|---|---|---|
| Kernel / `uname -r` | module target kernel 是誰 | headers 一定 matching |
| Build tree | external module 是否有 kbuild 入口 | module 一定能 load |
| make/gcc/git | 基本工具是否存在 | toolchain/config 完整 |
| debugfs mounted | Lab01 observation surface 可用 | debugfs entry 已建立 |
| Secure Boot state | unsigned module 是否可能被 policy 擋 | load failure 一定由它造成 |
| taint | kernel 是否已有值得記錄的 taint flags | taint=0 代表 driver 正確 |

### Matching build tree

External module 的典型建法：

```sh
make -C "/lib/modules/$(uname -r)/build" M="$PWD"
```

`KDIR` 只是常見變數名；它通常指向 running kernel 的 build tree。只有任意一份 kernel headers 不夠。

## Debugfs

檢查：

```sh
grep ' /sys/kernel/debug ' /proc/mounts
```

需要時：

```sh
./scripts/mount-debugfs.sh
```

Debugfs 是開發觀測介面，不是 stable product UAPI。它未掛載只會讓你看不到 surface，不等於 module init 一定失敗。

## Secure Boot、signature 與 lockdown

```sh
mokutil --sb-state 2>/dev/null || true
sudo dmesg | tail -n 100
```

若 `insmod` 被拒絕，依序查：

1. running kernel/architecture/build tree；
2. `modinfo module.ko` 的 vermagic/architecture/license；
3. kernel loader log；
4. signature、Secure Boot/lockdown；
5. dependency、symbol/version 與 module policy。

不要用 `rmmod -f` 或關閉安全機制來掩蓋 lifecycle bug。

## QEMU EDU gate

在 guest 內：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

看不到 EDU 時，問題早於 driver binding/probe：先查 QEMU command line、machine/device model、guest PCI enumeration，不要先改 `probe()`。

進一步文件：

- [`../../qemu/README.md`](../../qemu/README.md)
- [`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)
- [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
- [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)

## 常用命令

```sh
# Build/load/unload
make
modinfo ./module.ko
sudo insmod ./module.ko
lsmod | grep module_name
sudo rmmod module_name

# Kernel evidence
sudo dmesg | tail -n 100
journalctl -k -n 100

# PCI evidence
lspci -Dnn
lspci -Dnnk -d 1234:11e8
```

## 不建議的環境捷徑

- 不在 macOS 直接載入 Linux `.ko`。
- 不把 Docker container 當 PCI/kernel-module runtime environment；container 分享 host kernel，通常也看不到你需要的 PCI/QEMU hierarchy。
- 不因 compile pass 就跳過 matching runtime kernel。
- 不把固定 BDF、IRQ number、page size 或 QEMU accel 寫成通則。

## Self-check

1. 為什麼 module 要對 running kernel build tree 編譯？
2. `check-kernel-env.sh` pass 能證明什麼、不能證明什麼？
3. Mac ARM host 跑 x86_64 guest 為什麼通常使用 TCG？
4. Guest `lspci` 看不到 EDU 時，問題位於 probe 之前還是之後？
5. `Invalid module format` 的第一輪排查順序是什麼？

<details>
<summary>參考答案</summary>

1. External module 依賴 target kernel headers/config/symbol/version/architecture；任意 headers 可能產生不相容 vermagic 或 ABI。
2. 只證明基本環境與風險檢查完成；不證明 source、load、callback、MMIO/IRQ/DMA 行為正確。
3. KVM/HVF 通常只加速相同 host/guest ISA；跨 ISA 需 QEMU software translation/emulation。
4. 之前。沒有被 PCI core 枚舉的 `pci_dev`，driver core 就沒有 match/bind/probe target。
5. running kernel/arch/build tree → modinfo/vermagic → loader dmesg → signature/lockdown → dependency/symbol policy。

</details>

## 來源與查證

- External modules: <https://docs.kernel.org/kbuild/modules.html>
- Module signing: <https://docs.kernel.org/admin-guide/module-signing.html>
- Debugfs: <https://docs.kernel.org/filesystems/debugfs.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
'''

KERNEL_INTERFACES = r'''# Kernel 介面地圖：VFS、filesystem surfaces 與 API 參數角色

> **定位**：集中解釋 `/dev`、sysfs、procfs、debugfs、VFS callbacks、usercopy 與 kernel API 參數。讀 Labs01～04 時不必在多份 bridge 文件之間跳轉。

## 先講結論

Linux driver 常把同一個 object 從不同 surface 暴露給 userspace，但每個 surface 的責任不同：

```text
/dev node        → 穩定 data/control UAPI 的常見入口
sysfs            → device model 與簡單 attributes
/proc            → process/system information 或 legacy/reporting surface
debugfs          → 開發者 debug state/knobs，沒有 stable ABI 承諾
```

Userspace 的 `read/write/ioctl/mmap` 先進 syscall/VFS/subsystem，再由 kernel 呼叫 driver callback。Path 存在只證明某層 registration 成功，不證明 callback、payload 或 lifetime 全部正確。

## 不確定處與驗證狀態

- 具體 sysfs/debugfs/UAPI policy 依 subsystem 與產品安全需求。
- Devtmpfs/udev、permissions、container namespace 與 distro policy 會影響 `/dev` surface。
- Kernel API signatures 會隨版本改變；以 target headers/current source 為準。

## Filesystem surfaces 分工

| Surface | 誰建立/管理 | 適合內容 | 不適合內容 |
|---|---|---|---|
| `/dev/<node>` | cdev/device model + devtmpfs/udev | read/write/ioctl/mmap/poll UAPI | 任意 internal struct dump |
| `/sys/class/...` | device model/class/device | identity、simple state/attributes | 大量高速 payload |
| `/sys/bus/pci/...` | PCI core/device model | BDF、vendor/device、resources、binding | device-specific command data plane |
| `/proc/devices` | char/block registration reporting | major/name evidence | device readiness proof |
| `/sys/kernel/debug/...` | debugfs | counters、last error、debug trigger | stable product ABI |

### Lab 對應

- Lab01：debugfs entries 與 logging。
- Lab02：dev_t → cdev → class/device → `/dev`、sysfs、`/proc/devices`。
- Lab03：同一 node 上的 read/write/ioctl/poll/mmap。
- Lab05～07：PCI sysfs、driver binding、resources 與 IRQ evidence。

## VFS callback 心智模型

```text
userspace read(fd, ...)
→ syscall entry
→ VFS 找到 struct file
→ file_operations.read callback
→ driver validates/copies/updates state
→ return bytes or negative errno
```

同一 task 進 kernel 不必然 task context switch；callback 若 block，scheduler 才可能換 task。

## 常見 kernel object 角色

| Object | 角色 | Lifetime 問題 |
|---|---|---|
| `dev_t` | major/minor device number | 成功 alloc 後必須 unregister |
| `struct cdev` | dev_t 與 file_operations registration | open callbacks 前要 live，destroy 前阻止新使用 |
| `struct class/device` | device model/sysfs surface | node/symlink/attributes 可能被 userspace 使用 |
| `struct file` | 每次 open file description | 可保存 per-open private_data/f_pos |
| global/per-device state | 多 open/callback 共用 | 需 lock/refcount/quiesce |
| VMA/backing page | mmap mapping 與實體 page | fd close 不必然結束 VMA lifetime |

## API 參數固定分類

看到任何 kernel API，先將參數標成下列角色：

| 角色 | 問法 | 例子 |
|---|---|---|
| input value | caller 提供什麼設定？ | size、flags、count、direction |
| output parameter | API 成功後寫回什麼？ | `dma_addr_t *`, allocated dev_t |
| previous resource | 是否使用前一步取得的 object？ | `pdev`, `cdev`, `class`, `dev` |
| owner/context | 錯誤、lifetime、logging 掛在哪個 object？ | `struct device *` |
| callback table/function | 之後事件發生時呼叫誰？ | `file_operations`, IRQ handler |
| identity/name | kernel/user 如何辨識？ | driver/module/device name |
| cleanup token | 釋放時必須傳回同一個什麼？ | IRQ `dev_id`, DMA handle/size |
| userspace pointer | 是否需 usercopy/access check？ | `char __user *`, ioctl arg |

### 範例：char device registration

```text
alloc_chrdev_region(&devt, first_minor, count, name)
                   └ output          └ quantity/identity

cdev_init(&cdev, &fops)
          └ resource └ callback table

cdev_add(&cdev, devt, count)
         └ object  └ assigned range
```

### 範例：DMA allocation

```text
dma_alloc_coherent(dev, size, &dma_handle, gfp)
                   └owner └size └output DMA addr └context
```

CPU 使用 return pointer，device 使用 `dma_handle`。Output 參數不是 optional decoration。

## Usercopy 與 return convention

- `copy_to_user()` / `copy_from_user()` 回傳未成功複製的 bytes，0 才是完整成功。
- Usercopy 可能 fault/sleep，不可放在 hard IRQ 或持 spinlock 的 atomic context。
- `read/write` 成功回實際 bytes；錯誤回負 errno。
- Partial I/O 是合法 outcome，UAPI/runtime 必須明確處理。

## Resource 與 lifetime 讀法

每看到 create/alloc/register/map/request：

1. 何時開始 live？
2. 誰可能並行使用？
3. Failure path 哪一個 label 撤銷？
4. Remove 前要先停止哪個 producer/waiter/callback？
5. Cleanup 需要同一 size、handle、dev_id 或 object 嗎？

不要只記「API 倒序 free」。IRQ、work、DMA、open fd、VMA 會跨越簡單 call stack。

## 常見誤解

- `/dev` node 存在 ≠ driver data path 正確。
- Sysfs path 存在 ≠ device operation ready。
- Debugfs 可讀寫 ≠ stable/security-reviewed UAPI。
- `file_operations` callback ≠ userspace function pointer。
- Kernel pointer ≠ userspace pointer ≠ DMA address ≠ `__iomem` mapping。
- Kernel mutex不能直接鎖住 arbitrary userspace mmap load。

## Self-check

1. `cdev_add()` 與 `device_create()` 分別建立哪一層？
2. `/proc/devices` 出現名稱能證明什麼？
3. API output parameter 為什麼常是後續 cleanup/硬體 programming 的關鍵？
4. Usercopy 為什麼不能放在 spinlock/hard IRQ path？
5. Fd close 後 mmap backing object 是否一定可以立刻 free？

<details>
<summary>參考答案</summary>

1. Cdev registration 把 dev_t 綁到 file_operations；device_create 建立 device-model/sysfs surface，devtmpfs/udev 才可能建立 node。
2. 只證明 char/block major registration report 中有該名稱；不證明 node、open、callback、payload 或 cleanup 正確。
3. 它可能是 assigned dev_t、DMA address、vector count 等下一步唯一可用 token；cleanup 也常必須傳回相同 object/size/ID。
4. Usercopy 可能 page fault 或 sleep；spinlock/hard IRQ context 不允許 blocking/scheduling。
5. 不一定。VMA 可以在 fd 關閉後繼續存在；driver 要管理 mapping/backing page 的獨立 lifetime。

</details>

## 來源與查證

- Driver basics: <https://docs.kernel.org/driver-api/basics.html>
- Debugfs: <https://docs.kernel.org/filesystems/debugfs.html>
- Sysfs: <https://docs.kernel.org/filesystems/sysfs.html>
- Memory mapping APIs: <https://docs.kernel.org/core-api/mm-api.html>
'''

CONCURRENCY = r'''# Concurrency primer — shared state、ordering、waiting 與 lifetime

> **定位**：進 Lab04 前的唯一 concurrency 前導；也作為 Labs06/07 IRQ/DMA teardown 的共同心智模型。

## 先講結論

Concurrency 問題不只是在 counter 周圍加 lock。你要先分清五層：

```text
mutual exclusion  ：誰能同時修改 shared invariant
atomicity         ：單次更新能否被拆開/插入
memory ordering   ：不同 access 對 observer 可見的先後
waiting/completion：誰在等哪個 predicate/event
lifetime/quiesce  ：resource free 前誰仍可能使用
```

同一份 driver state 可能同時被 syscall callbacks、kthread/workqueue、hard/threaded IRQ、timer、remove/reset 與 device DMA 使用。只列 userspace threads 會漏掉真正的 execution paths。

## 不確定處與驗證狀態

- Lock/context implementation 會受 PREEMPT_RT、architecture 與 kernel config 影響。
- KCSAN/lockdep/stress 增加 evidence，不是形式化證明。
- Device ownership/ordering 還需 DMA/MMIO/device protocol；CPU lock 不能取代。

## 名詞先說清楚

| 名詞 | 意思 | 不代表什麼 |
|---|---|---|
| execution path | 可能獨立交錯執行的 callback/thread/IRQ/device action | 只指 process/thread |
| shared state | 多 path 可讀寫或其 lifetime 相互依賴的 object | 只指 global variable |
| data race | 未同步的 concurrent conflicting access 到同一 location | 所有 logical race |
| race condition | 結果依事件順序而可能破壞邏輯 | 必然是 C data race |
| invariant | 多個欄位/步驟必須共同成立的條件 | 單一變數值 |
| in-flight | 已開始、尚未證明退出/完成的使用者或工作 | CPU 正在某行 code 而已 |
| quiesce | 停新 producer 並同步既有使用者 | 只寫 stop flag |

## 心智模型：共享帳本與關店

`counter++` 實際是 read → add → write。兩人同讀舊值會 lost update。Mutex 像鎖住整段帳本修改。

Teardown 則像關店：先停止接新單、停 worker/device producer、等店內人離開，最後才拆櫃台和倉庫。只把 create API 倒著呼叫，無法處理晚到 IRQ/DMA/VMA。

## 四步 concurrency map

1. **列 state**：counter、queue indices、status、mapping、pointer、refcount。
2. **列 paths**：read/write/ioctl、worker、IRQ、timeout、remove、DMA。
3. **列 contract**：mutex/atomic/release-acquire/wait predicate/ownership。
4. **列 lifetime**：誰 publish、誰 stop、誰 join/synchronize、何時 free。

## 工具分工

| 工具 | 適合 | 不解決 |
|---|---|---|
| mutex | 可睡 task context 的 multi-step invariant | hard IRQ、device completion |
| spinlock | 短 atomic/IRQ-shared critical section | sleep/usercopy/long work |
| atomic RMW | 單一 counter/bit/reference update | multi-field invariant |
| READ_ONCE/WRITE_ONCE | marked single access/compiler discipline | lock、general publication |
| acquire/release/barrier | memory visibility/order | mutual exclusion、wait hardware |
| wait queue | 等 predicate，wake 後 recheck | 保存 payload、保證 ready |
| completion | 一次性/計數完成通知 | device source ACK、persistent queue |
| kthread_stop | stop request + wait thread return | IRQ/timer/DMA 等其他 producer |
| synchronize_irq | 等 in-flight handler 退出 | 停 device 產生新 IRQ |
| refcount/kref | object users 的 lifetime accounting | protocol ordering |

## 正確範式與反例

### Lost update

```c
mutex_lock(&lock);
counter++;
mutex_unlock(&lock);
```

Lock 必須包住完整 RMW。只在 read 或 write 一側上鎖仍可能 lost update。

### Publication

```c
data = value;
smp_store_release(&ready, 1);

if (smp_load_acquire(&ready))
    use(data);
```

`WRITE_ONCE(ready, 1)` 單獨不建立一般 CPU publication contract。

### Wait predicate

```text
while predicate is false:
    sleep on wait queue
wake → acquire protection → recheck predicate
```

兩 reader 被同一 event 喚醒後，第一個可能已消費 record；第二個必須 recheck。

### Teardown

```text
unpublish/reject new work
→ stop/mask producer
→ wait/join/synchronize in-flight paths
→ free resources in dependency order
```

錯誤範例是只設 bool 後立即 free，或 unmap BAR/free DMA buffer 後才關 IRQ/device。

## Lab 對應

- Lab03：mutex 只保護 kernel writers；mmap snapshot 另用 sequence publication。
- Lab04：lost update、mutex、initialize-before-kthread、`kthread_stop()`。
- Lab06：ACK/mask source，`synchronize_irq()` 後再 free state/MMIO。
- Lab07：completion/idle、DMA ordering、quiesce-before-free。
- Lab09：parallel/reload、KCSAN/lockdep/fault injection roadmap。

## Debug 問法

- 哪兩條 path 可同時執行？
- 它們是否碰同一 location，或碰不同欄位卻共享 invariant？
- Lock 是否在所有 access paths 使用同一 contract？
- Wake 的 predicate 是否在保護下 recheck？
- Timeout/remove 後是否可能有 late completion？
- Object free 前是否同步所有 fd/VMA/work/IRQ/DMA users？

## Self-check

1. Data race 與 race condition 差在哪？
2. `atomic_inc()` 為什麼不一定保護 multi-field state？
3. Wakeup 後為什麼還要 recheck predicate？
4. `synchronize_irq()` 為什麼不能取代 device mask/ACK？
5. Barrier、lock、completion、quiesce 各解哪一層？

<details>
<summary>參考答案</summary>

1. Data race 是同一 location 的未同步 conflicting access；race condition 更廣，atomic operations 的事件順序也可能破壞邏輯。
2. 它只讓單一 RMW 原子；counter、list、owner、state 之間的 invariant 仍可能被其他 path 看到不一致。
3. Wake 只表示狀態可能改變；其他 reader/consumer 可在你取得保護前先消費或清除條件。
4. 它只等待已進 handler 的執行者退出；未 mask/ACK 的 device 可繼續產生新 event。
5. Lock 解互斥，barrier 解可見順序，completion 解已發生事件的等待，quiesce 解 free 前停止/同步所有 producers/users。

</details>

## 來源與查證

- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- Mutex design: <https://docs.kernel.org/locking/mutex-design.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- Wait/completion/kthread basics: <https://docs.kernel.org/driver-api/basics.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
'''

ACCEL_ARCH = r'''# Accelerator host-driver architecture — 從 labs 到真實產品

> **定位**：把十個 labs 對映到 AI/HPC accelerator 的 host-side software stack，並誠實標出仍缺少的 production work。

## 先講結論

典型 accelerator software 不是單一 `.ko`，而是一組跨層 state machines：

```text
application / framework
→ userspace runtime / compiler integration
→ UAPI / queue / memory management
→ kernel PCI driver
→ BAR/MMIO control + IRQ/CQ + DMA/IOMMU
→ device firmware / engines
→ reset / recovery / telemetry
```

`driver-lab` 已覆蓋 Linux host-driver 的共通骨架，但沒有 vendor 真卡、firmware boot、production queue、security-reviewed UAPI 或完整 reset/error recovery。正確說法是「已建立可重現 baseline」，不是「只差 vendor registers」。

## 不確定處與驗證狀態

- 不同 accelerator 的 split 可能偏 kernel、userspace、firmware 或 subsystem framework。
- Queue/register/firmware/reset protocol 完全 device-specific。
- QEMU EDU 的單 buffer、單 vector、probe-time self-test 不能代表 production throughput/latency。
- 職缺內容會變，投遞時要回官方 JD 重查。

## 典型元件與責任

| 層 | 常見責任 | 主要 failure/lifetime |
|---|---|---|
| application/framework | graph/model/work submission | cancellation、process death |
| userspace runtime | device discovery、context、buffer/queue API、poll/event | fd/mapping ownership、ABI version |
| UAPI | ioctl/mmap/poll、handles、memory pin/map | hostile input、compat、security |
| PCI kernel driver | probe/remove、BAR、IRQ、DMA/IOMMU | hot-unplug、reset、AER、PM |
| queue engine | descriptor/CQ/doorbell、scheduling | wrap、ownership、timeout、backpressure |
| memory manager | coherent/streaming/SG/pinned memory/IOVA | unmap-before-idle、isolation |
| firmware | boot、command protocol、health | hang、version mismatch、recovery |
| observability | logs、tracepoints、counters、crash dump | perturbation、privacy、volume |

## Labs 對映

| Lab | 已建立的能力 | Production 還要補 |
|---|---|---|
| 00 | module lifecycle、failure unwind | PCI bind、PM、hotplug/error recovery |
| 01 | debugfs/log observation | tracepoints、health/telemetry policy |
| 02 | cdev/read-write boundary | versioned/security-reviewed UAPI |
| 03 | ioctl/poll/mmap、snapshot | handles、pinning、multi-process lifetime |
| 04 | race/mutex/kthread stop | per-queue locking、RCU/refcount、cancel/reset races |
| 05 | PCI bind、BAR/MMIO | full register protocol、power/firmware bring-up |
| 06 | one vector、status/ACK | MSI-X multi-queue、affinity/coalescing |
| 07 | coherent round-trip | descriptor rings、streaming/SG、IOMMU、performance |
| 08 | userspace wrapper/CLI | production runtime、context/session/API compatibility |
| 09 | reload/parallel scaffold | sanitizer/fault matrix、CI hardware farm |

## 一筆 command 的完整故事

```text
runtime validates request
→ allocate/reserve queue slot and memory mapping
→ fill descriptor/payload
→ publish ownership with correct ordering
→ ring MMIO doorbell
→ device/firmware executes
→ DMA writes completion/payload
→ MSI-X or polling exposes CQ entry
→ driver/runtime reclaims ownership
→ validate status/length/sequence
→ release resources only after all users stop
```

每一步都可能有 timeout、process exit、reset、hot-unplug 或 stale completion；產品設計需要 generation/tag/cancellation/recovery，而不只 happy path。

## Resource / state machine 表

面試或 design review 時至少能畫：

| Resource/state | 建立 | 開始被誰使用 | 停止條件 | 釋放 |
|---|---|---|---|---|
| PCI function/BAR | probe | control paths | remove/reset reject new work | unmap/release/disable |
| IRQ vectors | probe/queue setup | device + handlers | source masked/ACK、handlers synchronized | free IRQ/vectors |
| DMA mapping | buffer/queue setup | CPU/device ownership protocol | completion/abort/reset proves idle | unmap/free/unpin |
| queue/context | open/ioctl/runtime | process, workers, IRQ/CQ | cancel/drain/refcount zero | destroy state |
| firmware state | probe/reset | all commands | health/stop/reset protocol | reinitialize or device unavailable |

## 作品如何誠實描述

可以說：

> 我以 current Linux/QEMU source 建立了可重現的 host-driver baseline：module/UAPI/concurrency、PCI BAR/MMIO、IRQ、coherent DMA、userspace runtime與 stress scaffold。作品明確區分 static、compile、QEMU runtime 與 real-hardware gap；下一步是 descriptor ring、streaming/SG、MSI-X multi-queue、IOMMU 及 device-specific reset/firmware recovery。

不要說：

- 已完成 production accelerator driver；
- QEMU IRQ/DMA 等同真卡 bring-up；
- 只剩下 register map；
- compile/smoke pass 證明 race-free。

## 高價值下一步

1. 實作 coherent descriptor ring：OWN/phase/index、`dma_wmb/rmb`、wrap/full/empty。
2. 加 streaming/SG payload 與 IOMMU on/off/SWIOTLB tests。
3. Lab06 擴成多 vector/per-queue state/affinity。
4. 可控制地注入 IRQ timeout、command timeout、reset success/failure。
5. 讀一支 upstream accelerator/NVMe/network driver，畫 resource/lifecycle 表。
6. 保存 target kernel/QEMU/device SHA、commands、logs 與 bug diary。

## Self-check

1. Runtime、UAPI、kernel driver、firmware 各自解什麼？
2. 一個 IRQ/CQ 到達為什麼不等於 payload 一定正確？
3. Descriptor ring 比 Lab07 single-buffer path 多哪些 contract？
4. Process exit/reset/remove 為什麼是 queue/mapping lifetime 問題？
5. 如何客觀描述 QEMU EDU 作品而不誇大？

<details>
<summary>參考答案</summary>

1. Runtime 管 application-facing context/handles；UAPI 定義跨 boundary contract；kernel driver 管 PCI/resources/isolation；firmware 管 device-specific engines/protocol。
2. Notification 只證明 event path；錯誤 address、length、direction、stale tag 或 corruption 仍可能存在，需 status/sequence/compare。
3. Slot reservation、多 producer、OWN/phase publication/reclaim、wrap/full/empty、CQ/doorbell、backpressure、timeout/cancel與 lifetime。
4. 非同步 device/IRQ/worker 可能仍持有 queue或 DMA address；free/unmap 前要拒絕新 work、drain/abort/reset並同步 refs。
5. 說清楚 target、已實跑層級、可重現 evidence、已建立的通用骨架，以及 real hardware/production gaps。

</details>

## 來源與查證

- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- VFIO/IOMMUFD: <https://docs.kernel.org/driver-api/vfio.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
'''

DEBUGGING = r'''# Driver debugging — 從 symptom 到可重現 regression

> **定位**：集中取代分散的 code-reading guide、common failures 與 debugging playbook。遇到問題先定位最早失敗的 layer，再選工具。

## 先講結論

Debug 不是隨機試 API，而是：

```text
symptom
→ 分層與假設
→ 最小實驗
→ run-specific evidence
→ 根因
→ 最小修正
→ regression / fault test
```

越早失敗的 gate 越有資訊。`lspci` 看不到 device 時，不要先改 probe；probe 未進時，不要先查 DMA compare；IRQ 未發生時，不要只改 `dma_rmb()`。

## 不確定處與驗證狀態

- Tool availability、overhead、false positive/negative 依 kernel config/architecture。
- QEMU 能控制部分 failure，但不涵蓋 real board/firmware/link faults。
- Sanitizer/stress 沒報告不等於 bug 不存在。

## 讀 source 的固定順序

1. **入口**：module init、PCI probe、file_operations、IRQ handler、worker、remove。
2. **state**：global/per-device/per-open/per-queue fields。
3. **resource acquire**：alloc/register/request/map/enable/start。
4. **normal flow**：誰寫什麼、誰觀察什麼、completion evidence。
5. **failure unwind**：每個成功步驟的對應撤銷。
6. **teardown**：unpublish → stop producer → synchronize → free。
7. **test oracle**：它如何知道真的成功/失敗？

每個 function 都寫下：caller、context、inputs/outputs、shared state、return convention、lifetime。

## 分層排查表

| Symptom | 第一個 layer | 首要 evidence |
|---|---|---|
| module build fail | toolchain/headers/source | compiler error、KDIR、uname |
| `insmod` fail | module loader/policy/init | modinfo、dmesg、signature/vermagic |
| `/dev` 不見 | cdev/device model/udev | init log、sysfs、`/proc/devices` |
| callback 沒進 | VFS/UAPI/path/permission | strace/CLI errno、dynamic debug |
| poll 不醒 | predicate/wakeup/concurrency | state before wake、poll mask、waiter |
| mmap snapshot 不穩 | publication/VMA/lifetime | seq begin/end、permissions、page size |
| `lspci` 無 EDU | QEMU/guest enumeration | launch args、lspci/sysfs |
| probe 不進 | ID/binding/policy | lspci -k、driver sysfs、probe return |
| BAR/MMIO fail | resource/mapping/register protocol | flags/len/request/iomap/width/readback |
| IRQ timeout | source/vector/BME/ACK/handler | vector mode、status、raise、ACK、count |
| DMA timeout | BME/address/domain/direction/command | DMA handle、register values、status/idle |
| compare fail | address/count/direction/ordering/data | TX/RX dump、status、completion sequence |
| unload warning/UAF | quiesce/lifetime | producer state、synchronize、sanitizer |

## Evidence 原則

- 不清全域 `dmesg`；記錄前後 cursor/line/time，只擷取本次新增訊息。
- Test 只卸載自己載入的 module。
- 不用 broad `|| true` 吞掉 crash/I/O error。
- 保存 exact command、stdout/stderr、dmesg、sysfs/IRQ/resource state。
- 一個 `passed` 字串不夠；IRQ/DMA 同時看 status/ACK/idle/payload/cleanup。

## 常用工具與邊界

| 工具 | 適合回答 | 不能單獨證明 |
|---|---|---|
| `dmesg` / journal | loader/callback/error path | timing/race absence |
| `lspci -Dnnvvk` | enumeration/resources/capability/binding | device firmware健康 |
| sysfs/proc/debugfs | current registration/state | hidden concurrent transition |
| dynamic debug | 選擇性 callsite log | high-rate performance |
| ftrace/tracepoints/perf | call/timing/scheduling | device wire protocol |
| lockdep | lock ordering/context misuse | data race/logic race全部 |
| KASAN | memory bounds/UAF class | 所有 timing/firmware bug |
| KCSAN | sampled data race evidence | 所有 interleaving/logic race |
| IOMMU fault logs | illegal DMA address/access | payload correctness |
| PCIe analyzer | TLP/link evidence | software ownership/lifetime全部 |

## Bug diary 模板

```text
Title / date
kernel, config, architecture
QEMU/device/firmware version
pcie-study SHA / driver-lab SHA
sanitizer/IOMMU state

Symptom:
Expected:
Exact command sequence:
Run-specific stdout/stderr/dmesg:
Resource/IRQ state before and after:

Hypothesis 1:
Experiment:
Evidence:
Result:

Root cause:
Fix and why the contract now holds:
Regression/fault test:
Remaining limits:
```

## 常見失敗的真正第一步

### `Invalid module format`

查 running kernel/arch/build tree → modinfo/vermagic → loader dmesg → signature/symbol policy。

### `/dev` 沒建立

查 init return → dev_t/cdev/class/device → sysfs → devtmpfs/udev/permission。Node 問題與 callback 問題分開。

### Poll/read 卡住

查 predicate 是否在保護下改變、wake 是否在 publish 後、waiter 是否在拿鎖後 recheck。

### PCI probe 沒進

查 enumeration、ID、existing driver、binding/policy、probe return。BDF 不 hard-code。

### IRQ 沒來

查 device source/status、vector allocation/mode、BME（MSI/MSI-X）、request、trigger write、ACK/mask。IRQ number本身通常不是第一根因。

### DMA 壞掉

先分 CPU pointer、DMA address、device-local address；再查 mask、direction/count、BME、completion/idle、ordering、payload compare。Timeout 後不盲 free。

## Fix review checklist

- 修正的是根因還是只延長 timeout？
- Contract 是否在所有 access path 一致使用？
- Error path 是否只撤銷已成功步驟？
- Remove/reset 是否先 stop/quiesce/synchronize？
- Test 在舊 bug 存在時真的會 fail 嗎？
- 是否新增了 run-specific evidence 與 remaining limits？

## Self-check

1. 為什麼「最早失敗的 gate」比最後症狀更有價值？
2. 為什麼不能 `dmesg -C`？
3. Sanitizer 無報告能否證明無 bug？
4. DMA compare fail 應先加 barrier，還是先分 address/domain/completion？
5. 一份可信 bug diary 最少要保存什麼？

<details>
<summary>參考答案</summary>

1. 後續失敗常是上游 gate 未成立的連鎖症狀；修最早 failure 可縮小假設空間。
2. Kernel log 是共享系統資源，清除會破壞其他 evidence；應用 cursor/time/line 隔離本次新增內容。
3. 不能；instrumentation、sample、執行路徑與 bug class 有限，只能增加 evidence。
4. 先分清 CPU/DMA/device-local address、mask/direction/count、operation completion與 payload；只有缺 ordering contract 時才選正確 barrier。
5. Environment/version/SHA、exact commands、expected/observed、run-specific logs/state、hypothesis/experiment/evidence、root cause、fix、regression與limits。

</details>

## 來源與查證

- Kernel testing overview: <https://docs.kernel.org/dev-tools/testing-overview.html>
- KASAN: <https://docs.kernel.org/dev-tools/kasan.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
- Lockdep: <https://docs.kernel.org/locking/lockdep-design.html>
- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
'''

COMPANION = r'''# Companion documents — 旁讀層，不是第二套主教材

> **定位**：說明 source 旁的 `*.c.md`、`*.h.md`、`*.sh.md`、`Makefile.md` 如何使用、何時可信，以及為何不再維護一份巨大逐檔索引與 rollout plan。

## 先講結論

主線只有：

```text
Lab README / canonical guide
→ current source and test
→ 必要時開同名 companion
→ 結論回到 source、official docs、runtime evidence
```

Companion 通常與 source 放在同一目錄並採 `<source-file>.md` 命名，因此不需要另一份數百行索引重複列出所有檔案。用 repository search 或直接找同名 `.md` 即可。

## 不確定處與驗證狀態

- 既有 companions 多數由先前 AI 生成，可能包含舊行號、舊 source behavior 或過度簡化。
- 它們沒有因 pedagogy pass 自動取得 `reviewed` 狀態。
- Generated doc 與 current source 不一致時，永遠以 current source/test/audit/official docs 為準。

## 何時適合開 companion

- 第一次 trace 一個較長 function，不知道 call flow。
- 想知道某個 Makefile/test script 的每段用途。
- 已先看 README 和 source，但需要逐段旁讀。

不適合：

- 用 companion 取代 current source。
- 依固定行號判斷 current behavior。
- 從 companion 的絕對句推導 kernel/device contract。
- 一開始同時打開所有 generated files。

## Authority order

1. Target kernel/QEMU/device reproduced behavior。
2. Official Linux/QEMU/device documentation。
3. Current `.c/.h/.sh` source and tests。
4. Current Lab README / reviewed canonical guides。
5. Generated companion documents。

## Reviewed companion 的最低條件

一份 companion 只有在以下條件成立後才能標為 reviewed：

- 指向 immutable source SHA 或與 current source 自動比對；
- function/symbol/resource/lifetime 與 current implementation 一致；
- 沒有把 QEMU/device-specific behavior 寫成通則；
- 正確區分 static、compile、runtime evidence；
- technical reviewer 與 beginner readability reviewer 都通過；
- source 改動後有 stale detection 或重新生成流程。

## 目前策略

- 不再繼續人工新增每一個小 wrapper 的 companion。
- 優先把主 README、concepts、runbook、tests 與 source comments 維持正確。
- 高價值 companion 可在 audit/main 合併、runtime logs 完整後重新生成。
- Rollout 計畫已收斂到這份政策，不再保留另一份容易過期的清單。

## 如何找到同名 companion

```sh
find labs runtime tests scripts qemu -name '*.md' | sort
find labs/07-pci-edu-dma -maxdepth 1 -name 'driver_lab_edu_dma.c*'
```

例如：

```text
labs/07-pci-edu-dma/driver_lab_edu_dma.c
labs/07-pci-edu-dma/driver_lab_edu_dma.c.md
```

## 來源與查證

- Current source tree and tests in this repository。
- Teaching standard: [`../TEACHING-QUALITY-STANDARD.md`](../TEACHING-QUALITY-STANDARD.md)
- Accuracy audit: [`accuracy-audit-2026-08.md`](accuracy-audit-2026-08.md)
'''

DOCS_INDEX = r'''# Driver-lab documentation — canonical map

## 先講結論

`docs/` 現在只保留少數 canonical entry points；不再要求初學者在十多份 onboarding/bridge 文件間跳轉。

```text
START-HERE
→ Lab README
→ concept / study-order guide（需要時）
→ current source + test
→ debugging/reference（失敗時）
```

## 新手入口

1. [`onboarding/START-HERE.md`](onboarding/START-HERE.md)
2. [`onboarding/linux-environment.md`](onboarding/linux-environment.md)
3. [`onboarding/kernel-interfaces.md`](onboarding/kernel-interfaces.md)
4. Lab00 → Lab09 的各自 `README.md`

## 核心 concepts

- [`concepts/concurrency-primer.md`](concepts/concurrency-primer.md)：shared state、locks、ordering、waiting、lifetime。
- [`concepts/pcie-primer.md`](concepts/pcie-primer.md)：PCI/BAR/MMIO/IRQ/DMA correctness-first 地圖。
- [`concepts/accelerator-driver-architecture.md`](concepts/accelerator-driver-architecture.md)：labs 如何對映到 accelerator software stack。

## Study-order / runbooks

- [`guides/lab-04-study-order.md`](guides/lab-04-study-order.md)
- [`guides/lab-05-study-order.md`](guides/lab-05-study-order.md)
- [`guides/qemu-edu-first-pass.md`](guides/qemu-edu-first-pass.md)
- [`guides/linux-guest-05-to-07-walkthrough.md`](guides/linux-guest-05-to-07-walkthrough.md)
- [`guides/linux-guest-05-to-07-checklist.md`](guides/linux-guest-05-to-07-checklist.md)

Walkthrough 用於第一次操作；checklist 只在已理解後重跑，不取代理解。

## Reference

- [`reference/debugging.md`](reference/debugging.md)：code reading、common failures、tools、bug diary、regression。
- [`reference/source-index.md`](reference/source-index.md)：official source entry points。
- [`reference/companion-docs.md`](reference/companion-docs.md)：generated companion 使用政策。
- [`reference/accuracy-audit-2026-08.md`](reference/accuracy-audit-2026-08.md)：技術訂正與 runtime gaps。

## Pedagogy / maintenance

- [`TEACHING-QUALITY-STANDARD.md`](TEACHING-QUALITY-STANDARD.md)
- [`PEDAGOGY-PASS-2026-08.md`](PEDAGOGY-PASS-2026-08.md)
- [`templates/LAB-README-TEMPLATE.md`](templates/LAB-README-TEMPLATE.md)
- [`pedagogy/migrated-docs.txt`](pedagogy/migrated-docs.txt)
- [`pedagogy/canonical-docs.txt`](pedagogy/canonical-docs.txt)

## Authority order

1. Target Linux/QEMU/device runtime evidence。
2. Official documentation。
3. Current source/tests。
4. Reviewed README/concepts/guides。
5. Generated companions。

Static/compile/smoke/stress/fault/real-hardware evidence 必須分開描述。

## Meta workflow

[`workflow/ai-agent-git-checkpoint-policy.md`](workflow/ai-agent-git-checkpoint-policy.md) 是 repo maintenance policy，不是 driver 學習主線。
'''

ROOT_README = r'''# driver-lab — 從 kernel module 到 PCIe MMIO / IRQ / DMA

> 一套可 build、load、觀察、故障排查與反覆驗證的 Linux host-driver labs。概念教材：[`chilung-cgu/pcie-study`](https://github.com/chilung-cgu/pcie-study)。

## 先講結論

十個 Lab 已使用同一套 beginner-first 結構：

```text
結論與驗證狀態
→ 問題與名詞
→ 心智模型
→ resource/data flow
→ current source
→ 正反範式
→ test evidence / debug order
→ limits / Self-check / sources
```

目前分支：

```text
main
  └─ review/accuracy-audit-2026-08
       └─ review/pedagogy-pass-2026-08
```

- Accuracy audit 修正 source、tests 與高風險技術語意。
- Pedagogy pass 保留 audit contract，改善全部 Lab README、核心 concepts、導航與 docs 結構。

**唯一新手入口：[`docs/onboarding/START-HERE.md`](docs/onboarding/START-HERE.md)**

## 不確定處與驗證狀態

- CI 已覆蓋 shell/Markdown/static style、userspace runtime build、Labs00～07 external-module compile、pedagogy structure 與 docs graph。
- 真正 `insmod/rmmod`、MMIO、IRQ、DMA、timeout/reset、sanitizer、IOMMU/SWIOTLB 仍需指定 Linux/QEMU guest logs。
- QEMU EDU 不是 production accelerator；不涵蓋 vendor firmware、PHY/link、完整 AER/PM/hotplug/reset、multi-queue MSI-X、pinned memory 與 security-reviewed UAPI。

## 學習路線

| Lab | 核心概念 | 第一層 evidence | 重要邊界 |
|---|---|---|---|
| 00 | module lifecycle | init/exit、parameters | failed init 自己 unwind |
| 01 | debugfs/logging | trigger/status/log | debugfs 非 stable UAPI |
| 02 | cdev/read-write | `/dev`/sysfs/proc/readback | 不是 multi-client queue |
| 03 | ioctl/poll/mmap | predicates、read-only snapshot | wake 只要求 recheck |
| 04 | race/mutex/kthread | unsafe/safe、stop | probabilistic test 非 proof |
| 05 | PCI/BAR/MMIO | enumeration/bind/liveness | read-back 非任意 command completion |
| 06 | IRQ | vector/status/ACK/complete | 先停 source 再 sync handler |
| 07 | coherent DMA | mask/transfers/idle/compare | 未 quiesce 不可 free |
| 08 | userspace runtime | unit/CLI/device UAPI | partial I/O、handle lifetime |
| 09 | stress/fault scaffold | reload/parallel oracle | 不是完整 fault framework |

## Host / guest

```text
macOS or Linux host
  └─ QEMU + guest image/network/storage
       └─ Linux guest
            ├─ matching kernel build tree
            ├─ QEMU EDU 1234:11e8
            └─ build/load/test Labs05～07
```

Labs00～04 可在合適 Linux host/guest；Labs05～07 需要 Linux PCI hierarchy 中的 EDU。Cross-ISA 通常使用 TCG。

## 快速開始

```sh
./scripts/check-kernel-env.sh
(cd labs/00-hello-module && ./test.sh)
```

依 [`START-HERE`](docs/onboarding/START-HERE.md) 逐關前進。進 PCI 前先讀 [`PCIe primer`](docs/concepts/pcie-primer.md)。

## Static/build gates

```sh
./scripts/quality.sh .
python3 scripts/check_pedagogy_structure.py
python3 scripts/check_docs_architecture.py
make -C runtime clean all
```

這些是必要 gate，不是 runtime proof。

## Runtime gates

Labs00～04：

```sh
for lab in \
  labs/00-hello-module \
  labs/01-debugfs-logging \
  labs/02-char-device \
  labs/03-ioctl-poll-mmap \
  labs/04-locking-and-races; do
  (cd "$lab" && ./test.sh)
done
```

EDU guest：

```sh
lspci -Dnn | grep '1234:11e8'
for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

Runtime report 至少記錄 kernel/QEMU/two-repo SHA、IOMMU/sanitizer state、commands、stdout/stderr/dmesg。

## Docs architecture

- [`Docs index`](docs/README.md)
- [`START-HERE`](docs/onboarding/START-HERE.md)
- [`Linux/QEMU environment`](docs/onboarding/linux-environment.md)
- [`Kernel interfaces`](docs/onboarding/kernel-interfaces.md)
- [`Concurrency primer`](docs/concepts/concurrency-primer.md)
- [`PCIe primer`](docs/concepts/pcie-primer.md)
- [`Accelerator architecture`](docs/concepts/accelerator-driver-architecture.md)
- [`Debugging`](docs/reference/debugging.md)
- [`Companion policy`](docs/reference/companion-docs.md)

重複的 onboarding bridge、roadmap、debugging 與 companion rollout 文件已整合到上述 canonical docs；全 repo local links 由 CI 驗證。

## 正確合併順序

1. 完成並合併 `driver-lab` accuracy audit。
2. `pcie-study` audit 鎖定 immutable merged driver SHA 後合併。
3. Rebase/retarget 兩個 pedagogy PR 到新 main 並重跑 CI/runtime。
4. 先合併 `driver-lab` pedagogy，再更新/合併 `pcie-study` pedagogy。
5. 最後重新生成並人工 review companion/NotebookLM artifacts。
'''

PEDAGOGY = r'''# Pedagogy pass — beginner-first、correctness-preserving

## 先講結論

本 branch 建立在 `review/accuracy-audit-2026-08` 上，已完成：

- Labs00～09 全部 primary README 的 beginner-first 改寫；
- PCIe、concurrency、accelerator architecture 三份核心 concept；
- 單一 START-HERE、環境、kernel interfaces；
- debugging 與 companion 政策集中化；
- 重複 onboarding/bridge/roadmap/reference 文件整併；
- structure、local-link、docs-architecture、static/build CI。

目標不是把內容說得簡單而已，而是讓讀者能由：

```text
心智模型
→ current source / resource / context / lifetime
→ test evidence
→ failure / limits
```

逐步建立可驗證理解。

## Technical baseline

Pedagogy 改寫不得撤銷 accuracy audit 的訂正，包括：

- syscall/IRQ entry 不等於 task switch；
- wakeup 不等於 predicate 成立；
- `READ_ONCE/WRITE_ONCE` 不等於 general barrier/lock；
- BAR raw/resource/`__iomem` 分離；
- normal MMIO ordering、posted arrival、device completion 分離；
- MSI/MSI-X 是 Memory Write Request；
- CPU pointer、DMA address、device-local address 分離；
- coherent/streaming ownership 與 ordering/completion/lifetime 分離；
- teardown 先 quiesce/synchronize，再 free。

## Evidence status

### 已完成

- source/document alignment review；
- all Lab README teaching structure；
- canonical docs consolidation；
- ShellCheck/Markdown/local link/checkpatch；
- userspace runtime/CLI build；
- Labs00～07 external-module compile；
- pedagogy/docs-architecture checks。

### 仍待完成

- target Linux runtime：Labs00～04；
- QEMU EDU runtime：Labs05～07；
- Lab03/04 concurrency + sanitizer；
- Lab06 repeated IRQ/teardown；
- Lab07 timeout/reset/IOMMU/SWIOTLB；
- real hardware/device-specific validation；
- generated companion regeneration/review。

## Canonical reader path

1. [`onboarding/START-HERE.md`](onboarding/START-HERE.md)
2. Each Lab `README.md`
3. Current source/test
4. Needed concept/study-order/runbook
5. [`reference/debugging.md`](reference/debugging.md) on failure
6. Companion only as secondary side reading

## Maintenance rule

A new top-level teaching document must answer a distinct reader question. Do not add another roadmap/bridge/checklist if existing canonical docs can absorb the content. New Lab details belong first in that Lab README; cross-lab theory belongs in one concept; operational repetition belongs in a walkthrough/checklist pair only when first-run and repeat-run use cases are genuinely different.

## Merge order

Keep this PR based on the audit branch until audit runtime/review is complete. After audit merges, rebase/retarget, rerun all gates, then merge `driver-lab` pedagogy before the companion `pcie-study` pedagogy PR.
'''

CANONICAL_PATHS = (
    "README.md",
    "docs/README.md",
    "docs/PEDAGOGY-PASS-2026-08.md",
    "docs/TEACHING-QUALITY-STANDARD.md",
    "docs/onboarding/START-HERE.md",
    "docs/onboarding/linux-environment.md",
    "docs/onboarding/kernel-interfaces.md",
    "docs/concepts/concurrency-primer.md",
    "docs/concepts/pcie-primer.md",
    "docs/concepts/accelerator-driver-architecture.md",
    "docs/guides/lab-04-study-order.md",
    "docs/guides/lab-05-study-order.md",
    "docs/guides/qemu-edu-first-pass.md",
    "docs/guides/linux-guest-05-to-07-walkthrough.md",
    "docs/guides/linux-guest-05-to-07-checklist.md",
    "docs/reference/debugging.md",
    "docs/reference/source-index.md",
    "docs/reference/companion-docs.md",
    "docs/reference/accuracy-audit-2026-08.md",
)

WRITES = {
    "README.md": ROOT_README,
    "docs/README.md": DOCS_INDEX,
    "docs/PEDAGOGY-PASS-2026-08.md": PEDAGOGY,
    "docs/onboarding/START-HERE.md": START_HERE,
    "docs/onboarding/linux-environment.md": LINUX_ENV,
    "docs/onboarding/kernel-interfaces.md": KERNEL_INTERFACES,
    "docs/concepts/concurrency-primer.md": CONCURRENCY,
    "docs/concepts/accelerator-driver-architecture.md": ACCEL_ARCH,
    "docs/reference/debugging.md": DEBUGGING,
    "docs/reference/companion-docs.md": COMPANION,
    "docs/pedagogy/canonical-docs.txt": (
        "# Canonical learner-facing documents after the 2026-08 consolidation.\n"
        + "\n".join(CANONICAL_PATHS)
        + "\n"
    ),
}

OLD_TO_NEW = {
    "docs/onboarding/reading-map.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/learning-dashboard.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/beginner-primer.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/beginner-glossary.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/lab-file-roles.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/lab-transition-map.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/00-to-01-debugfs-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/01-to-03-user-kernel-abi-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/03-to-05-concurrency-pci-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/05-to-07-pci-irq-dma-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/07-to-09-runtime-validation-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/guides/learning-roadmap.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/linux-host-setup.md": "docs/onboarding/linux-environment.md",
    "docs/onboarding/check-kernel-env-explained.md": "docs/onboarding/linux-environment.md",
    "docs/onboarding/kernel-filesystem-surfaces.md": "docs/onboarding/kernel-interfaces.md",
    "docs/onboarding/kernel-api-parameter-roles.md": "docs/onboarding/kernel-interfaces.md",
    "docs/guides/lab-04-walkthrough.md": "docs/guides/lab-04-study-order.md",
    "docs/reference/code-reading-guide.md": "docs/reference/debugging.md",
    "docs/reference/common-failures.md": "docs/reference/debugging.md",
    "docs/reference/debugging-playbook.md": "docs/reference/debugging.md",
    "docs/reference/companion-docs-index.md": "docs/reference/companion-docs.md",
    "docs/reference/companion-docs-rollout-plan.md": "docs/reference/companion-docs.md",
}

LINK_RE = re.compile(r"(?<!!)\[([^\]]+)\]\(([^)]+)\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "tel:", "data:")


def write_documents() -> None:
    for relative_path, content in WRITES.items():
        path = ROOT / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content.rstrip() + "\n", encoding="utf-8")


def remove_superseded() -> None:
    for relative_path in OLD_TO_NEW:
        path = ROOT / relative_path
        if path.exists():
            path.unlink()


def rewrite_links() -> None:
    mapping = {
        (ROOT / old).resolve(): (ROOT / new).resolve()
        for old, new in OLD_TO_NEW.items()
    }

    for markdown in sorted(ROOT.rglob("*.md")):
        if ".git" in markdown.parts:
            continue
        content = markdown.read_text(encoding="utf-8")

        def replace(match: re.Match[str]) -> str:
            label = match.group(1)
            destination = match.group(2).strip()
            wrapped = destination.startswith("<") and destination.endswith(">")
            raw = destination[1:-1].strip() if wrapped else destination
            if not raw or raw.startswith(SKIP_PREFIXES) or raw.startswith("#"):
                return match.group(0)

            path_part, _, _anchor = raw.partition("#")
            if not path_part or path_part.startswith("/"):
                return match.group(0)
            resolved = (markdown.parent / path_part).resolve()
            target = mapping.get(resolved)
            if target is None:
                return match.group(0)

            relative = os.path.relpath(target, markdown.parent.resolve()).replace(os.sep, "/")
            return f"[{label}]({relative})"

        rewritten = LINK_RE.sub(replace, content)
        if rewritten != content:
            markdown.write_text(rewritten, encoding="utf-8")


def main() -> int:
    write_documents()
    remove_superseded()
    rewrite_links()
    print(f"wrote {len(WRITES)} canonical files")
    print(f"removed {len(OLD_TO_NEW)} superseded files when present")
    print("rewrote local Markdown links to canonical destinations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
