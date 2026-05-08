# Linux Guest 操作手冊：從 05 跑到 07

這份手冊是給第一次在 `Linux guest` 內實際跑 `05-pci-edu-mmio`、`06-pci-edu-irq`、`07-pci-edu-dma` 的人。

目標不是解釋所有 PCIe 細節，而是讓你能夠：

1. 在 guest 內把環境準備好
2. 確認 `QEMU edu` 裝置真的存在
3. 依序跑通 `05 -> 06 -> 07`
4. 知道每一步成功時該看到什麼
5. 卡住時知道先看哪裡

> [!WARNING]
> 這三關一定要在 `Linux guest` 或可控制的 `Linux 主機` 上做。
> 不能在 `macOS` 直接 build/load Linux kernel module。

## 這份手冊的適用前提

- 你已經把 `driver-lab` 放進 guest
- 你現在人在 guest shell
- guest 內已經看得到 `QEMU edu`
- 你有 `sudo` 權限

如果上面任何一項不成立，先不要急著跑 `05-07`。

## 第 0 步：先確認你現在在哪裡

先跑：

```sh
pwd
uname -a
```

你應該至少確認兩件事：

1. 你真的在 `driver-lab` repo 裡
2. 你現在這台是 Linux，不是 macOS

## 第 1 步：確認 guest 內工具齊不齊

先跑：

```sh
command -v make
command -v gcc
command -v git
command -v lspci
command -v sudo
```

如果 `lspci` 找不到，在 Debian/Ubuntu 系統通常補：

```sh
sudo apt update
sudo apt install -y pciutils
```

如果 `make`、`gcc`、kernel headers 不齊，在 Debian/Ubuntu 系統通常補：

```sh
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

> [!NOTE]
> 不同發行版套件名稱可能不同。
> 你只要理解目標是：要有 `gcc`、`make`、`lspci`、以及對應目前 kernel 的 headers。

## 第 2 步：確認 kernel build tree 存在

先跑：

```sh
ls -ld /lib/modules/$(uname -r)/build
```

如果成功，你會看到類似：

```text
lrwxrwxrwx ... /lib/modules/6.x.y-.../build -> /usr/src/linux-headers-...
```

這代表：

- 之後 `make -C /lib/modules/$(uname -r)/build M=$PWD modules` 這條路是通的

如果失敗，先不要碰 `05-07` code。

先把 headers 補齊。

## 第 3 步：確認 QEMU EDU 真的在 guest 裡

跑：

```sh
lspci -nn | grep 1234:11e8
```

理想輸出類似：

```text
00:04.0 Class 00ff: 1234:11e8
```

如果沒有輸出，表示：

- 問題還在 QEMU/guest bring-up
- 不是 driver code 問題

這時先回去看：

- [edu-bringup-checklist.md](/Users/chilung/driver-lab/qemu/edu-bringup-checklist.md)
- [qemu-edu-first-pass.md](/Users/chilung/driver-lab/docs/qemu-edu-first-pass.md)

## 第 4 步：先確認 repo 自己的腳本沒有最基本問題

在 repo 根目錄跑：

```sh
./scripts/check-kernel-env.sh
./scripts/quality.sh .
```

你應該重點看：

- `Build tree` 有沒有存在
- `debugfs` 狀態
- `quality checks completed`

> [!NOTE]
> `quality.sh` 不是在幫你驗證硬體功能。
> 它只是先排除最基本的 shell 語法與部份風格問題。

## 建議的執行原則

請照這個順序：

1. 先做 `05`
2. `05` 穩了再做 `06`
3. `06` 穩了再做 `07`

不要一開始就直接跑 `07`。

原因很簡單：

- `07` 依賴 `PCI probe + BAR map + IRQ + DMA`
- 你如果連 `05` 都沒穩，後面會很難定位

---

## 第 5 步：跑 `05-pci-edu-mmio`

### 5-1. 進目錄

```sh
cd /path/to/driver-lab/labs/05-pci-edu-mmio
```

### 5-2. 先讀這關 README

```sh
sed -n '1,220p' README.md
```

你第一次至少要知道：

- 這一關只做 `PCI probe + BAR map + ident/liveness`
- 不碰 IRQ
- 不碰 DMA

### 5-3. 執行 smoke test

```sh
./test.sh
```

這支腳本會做：

1. 確認你在 Linux
2. 確認 `lspci` 看得到 `1234:11e8`
3. build `driver_lab_edu_mmio.ko`
4. 清空 `dmesg`
5. `insmod`
6. 從 `dmesg` 抓關鍵 log
7. `rmmod`
8. `make clean`

### 5-4. 成功時你應該看到什麼

理想上至少要看到：

```text
05-pci-edu-mmio smoke test passed.
```

如果你想自己看更完整的 kernel log，手動跑：

```sh
sudo dmesg | tail -n 50
```

你應該重點找：

```text
probe start
BAR0 mapped
ident=0x...
liveness check passed
```

### 5-5. 如果 `05` 失敗，先怎麼切

#### 情況 A：`guest 內看不到 QEMU edu`

先看：

```sh
lspci -nn | grep 1234:11e8
```

沒有的話，不要先看 driver code。

先回頭檢查 QEMU 啟動參數。

#### 情況 B：`make` 失敗

先看：

```sh
ls -ld /lib/modules/$(uname -r)/build
```

通常表示 headers 沒裝齊。

#### 情況 C：`insmod` 失敗

先看：

```sh
sudo dmesg | tail -n 50
```

常見原因：

- module build 成功，但 load 時 policy 擋住
- 或 driver `probe()` 直接錯誤返回

#### 情況 D：`liveness check failed`

先看：

- BAR map 有沒有真的成功
- register offset 是否正確
- `dmesg` 內 `wrote/read/expected` 三個值

### 5-6. `05` 通過後你應該知道什麼

你至少要能講出：

1. `probe()` 什麼時候會被叫
2. BAR0 是什麼
3. `pci_iomap()` 之後你拿到的是什麼
4. `0x04` liveness register 用來驗證什麼

---

## 第 6 步：跑 `06-pci-edu-irq`

### 6-1. 進目錄

```sh
cd /path/to/driver-lab/labs/06-pci-edu-irq
```

### 6-2. 先讀這關 README

```sh
sed -n '1,220p' README.md
```

你第一次至少要知道：

- 這一關是在 `05` 的基礎上加 IRQ
- 會用 `0x60` 人工 raise interrupt
- handler 裡要清 `0x64` acknowledge register

### 6-3. 執行 smoke test

```sh
./test.sh
```

### 6-4. 成功時你應該看到什麼

理想上至少要看到：

```text
06-pci-edu-irq smoke test passed.
```

手動看 log：

```sh
sudo dmesg | tail -n 80
```

你應該重點找：

```text
request_irq ok
irq status=0x...
irq self-test passed
```

### 6-5. 如果 `06` 失敗，先怎麼切

#### 情況 A：`request_irq failed`

先看：

- `05` 是不是真的穩
- `pci_alloc_irq_vectors()` 有沒有成功
- guest/kernel 是否支援目前這條 IRQ 路徑

#### 情況 B：timeout

這通常表示：

- 你有 raise interrupt
- 但 handler 沒進來，或 completion 沒完成

先看：

- `dmesg`
- `irq status`
- `acknowledge` 是否真的寫回

#### 情況 C：中斷一直重進

這通常表示：

- `0x64` acknowledge register 沒清乾淨

先不要急著改很多邏輯。

先確認：

- handler 裡有沒有真的執行 `iowrite32(..., 0x64)`

### 6-6. `06` 通過後你應該知道什麼

你至少要能講出：

1. IRQ handler 在做什麼
2. 為什麼要分 `interrupt status` 與 `acknowledge`
3. 為什麼這一關先做 top-half 的最小版本
4. 為什麼 completion 很適合拿來接 self-test

---

## 第 7 步：跑 `07-pci-edu-dma`

### 7-1. 進目錄

```sh
cd /path/to/driver-lab/labs/07-pci-edu-dma
```

### 7-2. 先讀這關 README

```sh
sed -n '1,260p' README.md
```

你第一次至少要知道：

- 這一關先用 `coherent DMA`
- QEMU EDU 預設 `dma_mask` 是 `28 bits`
- 這一關做的是：
  - RAM -> EDU
  - EDU -> RAM
  - 最後 `memcmp()` 比對 round-trip

### 7-3. 執行 smoke test

```sh
./test.sh
```

### 7-4. 成功時你應該看到什麼

理想上至少要看到：

```text
07-pci-edu-dma smoke test passed.
```

手動看 log：

```sh
sudo dmesg | tail -n 120
```

你應該重點找：

```text
dma mask configured
coherent buffer allocated
ram-to-edu transfer finished
edu-to-ram transfer finished
round-trip compare passed
```

### 7-5. 如果 `07` 失敗，先怎麼切

#### 情況 A：`dma_set_mask_and_coherent()` 失敗

先看：

- 你是不是照目前 lab 用 `DMA_BIT_MASK(28)`
- 有沒有亂改成更大的 mask

#### 情況 B：`dma_alloc_coherent` 失敗

先看：

- guest 記憶體夠不夠
- DMA mask 是否已正確設定

#### 情況 C：DMA timeout

先看：

- command register 的 start bit 是否有清掉
- IRQ 是否真的進來
- source / destination / count 是否寫對

#### 情況 D：`round-trip compare failed`

先看：

1. `RAM -> EDU` 的方向是否寫對
2. `EDU -> RAM` 的方向是否寫對
3. `DL_EDU_DEVICE_RAM_OFFSET` 是否合理
4. `count` 是否和 buffer 長度一致

### 7-6. `07` 通過後你應該知道什麼

你至少要能講出：

1. 為什麼要先設 DMA mask
2. 為什麼第一版先選 coherent DMA
3. `device-visible buffer` 和一般 kernel buffer 差在哪
4. 為什麼要同時看 IRQ 完成與 command bit 清除

---

## 建議的手動驗證方式

如果你不想一開始就跑 `./test.sh`，也可以手動拆開做。

### `05` 手動拆解

```sh
cd /path/to/driver-lab/labs/05-pci-edu-mmio
make
sudo dmesg -C
sudo insmod ./driver_lab_edu_mmio.ko
sudo dmesg | tail -n 50
sudo rmmod driver_lab_edu_mmio
make clean
```

### `06` 手動拆解

```sh
cd /path/to/driver-lab/labs/06-pci-edu-irq
make
sudo dmesg -C
sudo insmod ./driver_lab_edu_irq.ko
sudo dmesg | tail -n 80
sudo rmmod driver_lab_edu_irq
make clean
```

### `07` 手動拆解

```sh
cd /path/to/driver-lab/labs/07-pci-edu-dma
make
sudo dmesg -C
sudo insmod ./driver_lab_edu_dma.ko
sudo dmesg | tail -n 120
sudo rmmod driver_lab_edu_dma
make clean
```

---

## 建議你實際記錄的東西

第一次跑時，建議你自己另外記一份操作筆記，至少記：

1. 你跑了哪一條命令
2. 成功時的 `dmesg` 長什麼樣
3. 失敗時的第一個錯誤是什麼
4. 最後修正了哪一點

這樣你之後不只是「跑過一次」，而是有真的累積 bring-up 經驗。

---

## 你完成 `05-07` 後，代表什麼

如果你真的在 guest 內把 `05`、`06`、`07` 都跑通，你就已經不只是看過概念，而是實際做過：

- PCI device discovery
- BAR / MMIO 操作
- interrupt raise / handler / acknowledge
- coherent DMA buffer
- round-trip data verify

這已經是非常像 AI 加速卡 host driver 的基本骨架了。

## 如果你卡住，要回報哪些資訊

如果你之後要把錯誤貼回來，請至少帶這四項：

1. 你在做哪一關
2. 你執行的完整命令
3. `sudo dmesg | tail -n 100` 輸出
4. `lspci -nn` 裡 `1234:11e8` 那一行

這樣才能快速判斷是：

- 環境問題
- QEMU bring-up 問題
- module build/load 問題
- 還是 driver code 本身需要修正
