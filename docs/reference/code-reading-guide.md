# 程式閱讀指南

## 原則

- 不要一開始就讀最大、最複雜的 driver
- 先讀骨架清楚、責任單純的 driver
- 閱讀時永遠帶著三個問題：
  - `resource 是在哪裡拿到的？`
  - `錯誤路徑怎麼清？`
  - `remove / teardown 是否對稱？`

## 第一批必讀

### `drivers/uio/uio_pci_generic.c`

重點：

- PCI device bind / unbind
- generic PCI resource handling
- userspace 介面與 kernel 邊界的最小模型

### `drivers/misc/pci_endpoint_test.c`

重點：

- 如何用 host driver 驗證 BAR、IRQ、讀寫、copy
- 如何把「測試行為」包成 userspace 可呼叫的介面

### `drivers/nvme/host/pci.c`

重點：

- 真正成熟的 PCI storage driver 骨架
- queue、IRQ、DMA、teardown
- 觀察錯誤處理與 probe/remove 的結構

> [!NOTE]
> 不要試圖一次看完整支 `nvme/host/pci.c`。第一次只看：
> `probe`、resource init、IRQ 註冊、queue 初始化、remove。

## 第二批必讀

### `drivers/accel/*`

優先順序：

1. `drivers/accel/rocket/`
2. `drivers/accel/qaic/`
3. `drivers/accel/amdxdna/`

重點：

- 現代 Linux `accel` subsystem 的定位
- user-space runtime 與 kernel driver 的邊界
- buffer、job submission、device file、debugfs

## 建議閱讀方法

### 第一次

- 畫出主要資料流
- 找出 `probe/remove`
- 找出 `file_operations` 或等價 user API

### 第二次

- 列出所有主要 resource
- 對照 cleanup path
- 記錄 error label 與對應 rollback

### 第三次

- 把這支 driver 的角色翻譯成白話
- 說明它在系統中負責什麼，不負責什麼

## 輸出物

每看完一支 driver，建議在 [`../../notes/reading-log-template.md`](../../notes/reading-log-template.md) 寫一份摘要。

