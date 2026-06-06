# Kernel API 參數角色導讀

這份文件只解決一個問題：

> 第一次讀 kernel driver code 時，看到一串 kernel API 參數，要先懂到什麼程度？

答案是：第一輪要懂「參數角色」，不用追 API 內部實作。

也就是你要能說出：

- 這個參數是輸出結果嗎？
- 這個參數是前一步拿到的 resource 嗎？
- 這個參數是數量、名字、權限、callback table 嗎？
- 這個參數是 userspace pointer 嗎？
- 這個 API 成功後，下一步誰會用它？
- 失敗時，已經拿到的 resource 要怎麼清？

你第一輪不需要追：

- kernel 內部怎麼分配 major number
- `struct cdev` / `struct device` / `struct class` 的完整欄位
- VFS、kobject、sysfs、udev 的完整互動
- `copy_to_user()` 底層怎麼處理 page fault
- PCI core、IRQ core、DMA API 的完整內部實作

如果你卡住的是「這些 API 為什麼讓 `/dev`、`/sys/class`、`/proc/devices` 出現」，先看 [`kernel-filesystem-surfaces.md`](kernel-filesystem-surfaces.md)。那份解釋 filesystem 入口；這份只解釋 API 參數角色。

## 三輪閱讀深度

| 輪次 | 目標 | 問題 |
|---|---|---|
| 第一輪 | 懂參數角色 | 這個參數在本 lab 裡是 input、output、resource、數量、名字、callback，還是 userspace pointer？ |
| 第二輪 | 懂 lifetime / error path | 成功後誰會用它？失敗時跳到哪個 cleanup label？exit/remove 是否反向釋放？ |
| 第三輪 | 懂 kernel 內部 | kernel API 裡面如何管理 kobject、VFS、page fault、PCI config space、IRQ routing、DMA mapping？ |

讀 labs 時先用第一輪就好。你能把參數角色說清楚，已經比「只會照抄命令」前進很多。

## 讀任何 kernel API 的固定模板

| 問題 | 你要填的內容 |
|---|---|
| 這個 API 類型是什麼？ | 建立 resource、註冊 resource、使用 resource、等待事件、釋放 resource。 |
| 哪些參數是 input？ | 例如數量、名字、flag、權限、callback table、前一步拿到的 pointer。 |
| 哪些參數是 output？ | 例如 `&devt`、`&dma_handle`，kernel 會把結果填回來。 |
| 回傳值怎麼判斷？ | `0 / 負 errno`、`NULL`、`ERR_PTR()`、或實際 byte count。 |
| 成功後誰會用它？ | 下一個 API、VFS callback、IRQ handler、device register、userspace CLI。 |
| 失敗時怎麼清？ | 對照目前已成功取得的 resource，反向呼叫 cleanup API。 |

## 02-char-device：resource pipeline

這一關最重要的是看懂 `/dev/driver_lab_char0` 怎麼被建立出來。

```text
alloc_chrdev_region()
    產生 dl_char_devt
          ↓
cdev_init()
    把 dl_char_cdev 和 dl_char_fops 接起來
          ↓
cdev_add()
    把 cdev 掛到 dl_char_devt
          ↓
class_create()
    產生 dl_char_class
          ↓
device_create()
    用 class + dev_t + name 建出 device
```

### `alloc_chrdev_region()`

```c
ret = alloc_chrdev_region(&dl_char_devt, 0, 1, DL_CHAR_CLASS_NAME);
```

| 參數 | 角色 | 本 repo 的值 | 下一步誰會用 |
|---|---|---|---|
| `&dl_char_devt` | output；kernel 把分配好的 major/minor 填進來 | `dl_char_devt` | `cdev_add()`、`device_create()`、`unregister_chrdev_region()` |
| `0` | input；起始 minor number | minor 0 | 這個 lab 只有一個 device |
| `1` | input；申請幾個 device number | 1 個 | `cdev_add(..., count=1)` |
| `DL_CHAR_CLASS_NAME` | input；給 kernel 顯示/註冊用的名字 | `"driver_lab_char"` | 可在 `/proc/devices` 等地方輔助辨識 |

第一輪記法：

> 這一步向 kernel 申請一組 major/minor，結果存在 `dl_char_devt`。

### `cdev_init()`

```c
cdev_init(&dl_char_cdev, &dl_char_fops);
```

| 參數 | 角色 | 本 repo 的值 | 下一步誰會用 |
|---|---|---|---|
| `&dl_char_cdev` | output-like；初始化這個 char device 物件 | global `dl_char_cdev` | `cdev_add()` |
| `&dl_char_fops` | input；callback table | `.open/.read/.write/.release` | VFS 之後依這張表呼叫 driver callback |

第一輪記法：

> 這一步把「這個 char device」和「read/write 要呼叫哪個函式」接起來。

### `cdev_add()`

```c
ret = cdev_add(&dl_char_cdev, dl_char_devt, 1);
```

| 參數 | 角色 | 本 repo 的值 | 下一步誰會用 |
|---|---|---|---|
| `&dl_char_cdev` | input；已經被 `cdev_init()` 初始化的 cdev | `dl_char_cdev` | kernel VFS lookup |
| `dl_char_devt` | input；前面申請到的 major/minor | `dl_char_devt` | 把 device number 對到這個 cdev |
| `1` | input；這個 cdev 管幾個 minor | 1 個 | 只管理 `driver_lab_char0` |

第一輪記法：

> `cdev_init()` 只是準備物件；`cdev_add()` 才是真的註冊進 kernel。從這一步開始，這個 cdev 可能被 userspace open。

### `class_create()`

```c
dl_char_class = class_create(DL_CHAR_CLASS_NAME);
```

| 參數 | 角色 | 本 repo 的值 | 下一步誰會用 |
|---|---|---|---|
| `DL_CHAR_CLASS_NAME` | input；class 名字 | `"driver_lab_char"` | `device_create()` |
| 回傳值 | output；`struct class *` 或 error pointer | `dl_char_class` | `device_create()`、`class_destroy()` |

第一輪記法：

> 這一步建立 device model 的分類，主要是為了後面 `device_create()`。

### `device_create()`

```c
dl_char_device = device_create(dl_char_class, NULL, dl_char_devt, NULL,
                               DL_CHAR_DEVICE_NAME);
```

| 參數 | 角色 | 本 repo 的值 | 下一步誰會用 |
|---|---|---|---|
| `dl_char_class` | input；前一步建立的 class | `dl_char_class` | 決定 device 掛在哪個 class 下 |
| `NULL` | input；parent device | 無 parent | 這個 lab 沒有上層硬體裝置 |
| `dl_char_devt` | input；前面拿到的 major/minor | `dl_char_devt` | device node 對應的 device number |
| `NULL` | input；driver private data | 未使用 | 進階 driver 才常用 |
| `DL_CHAR_DEVICE_NAME` | input；device 名字 | `"driver_lab_char0"` | userspace 看到 `/dev/driver_lab_char0` |
| 回傳值 | output；`struct device *` 或 error pointer | `dl_char_device` | `device_destroy()` |

第一輪記法：

> 這一步把 class、dev_t、device name 組起來，讓 userspace 能看到 `/dev/driver_lab_char0`。

## 02-char-device：read/write helper

### `simple_write_to_buffer()`

```c
ret = simple_write_to_buffer(dl_char_buffer, DL_CHAR_BUFFER_SIZE - 1,
                             &pos, buf, count);
```

| 參數 | 角色 | 本 repo 的值 |
|---|---|---|
| `dl_char_buffer` | destination；kernel buffer | driver 保存 userspace 寫入資料的地方 |
| `DL_CHAR_BUFFER_SIZE - 1` | capacity；最多可寫多少 byte | 保留 1 byte 給 `'\0'` |
| `&pos` | offset input/output | 本 lab 每次 write 從 0 開始覆蓋 |
| `buf` | source；userspace pointer | `.write` callback 收到的 `const char __user *buf` |
| `count` | size；userspace 想寫幾個 byte | `.write` callback 收到的長度 |

第一輪記法：

> write path 是 user -> kernel，所以 userspace `buf` 是來源，kernel `dl_char_buffer` 是目的地。

### `simple_read_from_buffer()`

```c
ret = simple_read_from_buffer(buf, count, ppos,
                              dl_char_buffer, dl_char_buffer_len);
```

| 參數 | 角色 | 本 repo 的值 |
|---|---|---|
| `buf` | destination；userspace pointer | `.read` callback 收到的 `char __user *buf` |
| `count` | capacity；userspace buffer 可收多少 byte | `.read` callback 收到的長度 |
| `ppos` | offset input/output | 讓 read 像一般檔案一樣會往前讀 |
| `dl_char_buffer` | source；kernel buffer | driver 目前保存的資料 |
| `dl_char_buffer_len` | source length | kernel buffer 目前有效長度 |

第一輪記法：

> read path 是 kernel -> user，所以 kernel `dl_char_buffer` 是來源，userspace `buf` 是目的地。

## 回傳值也要分角色

| 形式 | 例子 | 第一輪判斷方式 |
|---|---|---|
| `int ret` | `alloc_chrdev_region()`、`cdev_add()` | `0` 成功，負 errno 失敗。 |
| pointer 或 error pointer | `class_create()`、`device_create()` | 用 `IS_ERR()` 判斷，`PTR_ERR()` 取出負 errno。 |
| byte count 或負 errno | `simple_read_from_buffer()`、`simple_write_to_buffer()` | 正數是 bytes，`0` 可能是 EOF 或寫入 0 byte，負數是錯誤。 |
| `NULL` pointer | `pci_iomap()`、`dma_alloc_coherent()` | `NULL` 代表失敗。 |

## cleanup 配對表

| init / probe 成功拿到 | cleanup |
|---|---|
| `alloc_chrdev_region()` | `unregister_chrdev_region()` |
| `cdev_add()` | `cdev_del()` |
| `class_create()` | `class_destroy()` |
| `device_create()` | `device_destroy()` |
| `pci_enable_device()` | `pci_disable_device()` |
| `pci_request_region()` | `pci_release_region()` |
| `pci_iomap()` | `pci_iounmap()` |
| `request_irq()` | `free_irq()` |
| `dma_alloc_coherent()` | `dma_free_coherent()` |

第一輪記法：

> 先拿的通常後放，後拿的通常先放。錯誤處理 label 就是在做「目前已經拿到哪些 resource，就清哪些」。

## 各 lab 第一輪要看的 API 參數

| Lab | 第一輪先看 |
|---|---|
| `00` | `module_param(name, type, mode)`：參數名字、型別、權限；`-EINVAL`：參數不合法時讓 `insmod` 失敗。 |
| `01` | `debugfs_create_file(name, mode, parent, data, fops)`：檔名、權限、父目錄、private data、callback table。 |
| `02` | char device pipeline：`dev_t`、`cdev`、`class`、`device`、`file_operations`。 |
| `03` | `ioctl arg` 是 userspace pointer；`copy_from_user()` / `copy_to_user()` 看方向；`poll_wait()` 把 fd 和 waitqueue 接起來。 |
| `04` | `mutex_lock()` 保護共享 state；`kthread_run()` 建背景競爭來源；UAPI struct 是 userspace/kernel 合約。 |
| `05` | `pci_enable_device(pdev)`、`pci_request_region(pdev, bar, name)`、`pci_iomap(pdev, bar, maxlen)` 都圍繞同一個 PCI device。 |
| `06` | `pci_alloc_irq_vectors()` 取得 IRQ vector；`request_irq(vector, handler, flags, name, dev_id)` 把 IRQ 接到 handler。 |
| `07` | `dma_alloc_coherent(dev, size, &dma_handle, gfp)` 同時給 CPU pointer 和 device DMA address。 |
| `08` | runtime helper 的參數多半是 userspace `handle`、path、buffer、timeout；它們不是 kernel API。 |
| `09` | script 參數是壓力測試控制值，例如次數、平行度、timeout；它們用來驗 cleanup 和共享狀態壓力。 |

## 參考官方文件

- [Linux Kernel API - Char devices](https://docs.kernel.org/5.19/core-api/kernel-api.html)
- [Linux Device Driver Infrastructure](https://docs.kernel.org/driver-api/infrastructure.html)
- [Linux seq_file](https://docs.kernel.org/filesystems/seq_file.html)
- [Linux DebugFS](https://docs.kernel.org/filesystems/debugfs.html)
- [Linux PCI driver guide](https://docs.kernel.org/PCI/pci.html)
- [Linux DMA API HOWTO](https://docs.kernel.org/core-api/dma-api-howto.html)
