# Kernel 介面地圖：VFS、filesystem surfaces 與 API 參數角色

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
