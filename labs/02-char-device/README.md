# 02 - Char Device

## 目的

建立最小的 user-kernel 邊界：

- 建立 `/dev/...`
- 實作 `open`
- 實作 `read`
- 實作 `write`

這一關會讓你開始從「只會 load module」進入「driver 對 userspace 提供介面」。

## 你會學到什麼

- `alloc_chrdev_region`
- `cdev_init` / `cdev_add`
- `class_create`
- `device_create`
- `copy_to_user` / `copy_from_user`
- 最小 resource cleanup path

## 先備理解

這一關開始，你不再只是「load 一個 module」。

你要開始理解：

- `/dev/driver_lab_char0` 是 driver 暴露給 userspace 的入口
- user space 的 `write()` 最後會走到你的 `.write` callback
- user space 的 `read()` 最後會走到你的 `.read` callback

## 這一關的資料流

```mermaid
sequenceDiagram
    participant U as userspace
    participant V as VFS
    participant D as driver_lab_char

    U->>V: write("/dev/driver_lab_char0", "hello")
    V->>D: dl_char_write()
    D-->>V: bytes written
    V-->>U: write() returns
    U->>V: read("/dev/driver_lab_char0")
    V->>D: dl_char_read()
    D-->>V: bytes from kernel buffer
    V-->>U: read() returns data
```

## 提供的裝置

```text
/dev/driver_lab_char0
```

## 手動操作

```sh
make
sudo insmod ./driver_lab_char.ko
printf '%s' 'hello-from-userspace' | sudo tee /dev/driver_lab_char0 >/dev/null
sudo dd if=/dev/driver_lab_char0 bs=1 count=20 status=none
sudo rmmod driver_lab_char
make clean
```

命令逐行在做什麼：

- `make`：建出 `driver_lab_char.ko`
- `insmod`：載入 char device module
- `tee /dev/driver_lab_char0`：把字串寫進 driver 的 kernel buffer
- `dd if=/dev/driver_lab_char0 ...`：再從 device node 把資料讀回來
- `rmmod`：卸載 module
- `make clean`：刪掉建置產物

## 自動化 smoke test

```sh
./test.sh
```

## user-space runtime

這個 lab 搭配：

- [`../../runtime/README.md`](../../runtime/README.md)
- [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c)

編譯 CLI：

```sh
cd ../../runtime
make
../tests/driver_lab_char_cli /dev/driver_lab_char0 write hello-runtime
../tests/driver_lab_char_cli /dev/driver_lab_char0 read
```

## 驗收標準

- `/dev/driver_lab_char0` 存在
- 可成功 write
- 可成功 read
- unload 時沒有 resource 洩漏或明顯錯誤

## 第一輪閱讀界線

| 分類 | 內容 |
|---|---|
| 第一輪必懂 | `/dev/driver_lab_char0` 是 userspace 入口；`write()` 會進 `dl_char_write()`；`read()` 會進 `dl_char_read()`；init 拿到的 device resource 要在 exit 反向釋放。 |
| 可以先略過 | `struct inode` / `struct file` 的完整內容；udev/devtmpfs 建立 device node 的完整流程；major/minor 編號管理的所有細節。 |
| 之後再回來補 | `cdev` lifetime、device class 與 sysfs 的關係、同一個 device 被多個 process 同時開啟時的語意。 |

## 完成後你應該能回答

| 問題 | 標準答案 |
|---|---|
| userspace 的入口在哪裡？ | `/dev/driver_lab_char0`；對它做 `read()` / `write()` 會經過 VFS 轉到 driver 的 `file_operations` callback。 |
| `.read` / `.write` 分別接到哪裡？ | `.read` 接到 `dl_char_read()`，`.write` 接到 `dl_char_write()`。 |
| 第一個觀測點是什麼？ | 寫入 `/dev/driver_lab_char0` 後讀回資料，並用 `dmesg` 觀察 `driver_lab_char` log。 |
| 這一關主要拿到什麼 resource？ | major/minor device number、`cdev`、`class`、`device`，最後由 udev 建立 `/dev/driver_lab_char0`。 |
| cleanup 要釋放哪些東西？ | `device_destroy()`、`class_destroy()`、`cdev_del()`、`unregister_chrdev_region()`，順序要大致反向於 init 拿資源的順序。 |
| `/dev/driver_lab_char0` 沒出現時第一個看哪裡？ | 先看 `sudo dmesg | tail -n 50`，再查 `lsmod` 與 `ls -l /dev/driver_lab_char0`。 |

## 目前這支 driver 的刻意簡化

- 每次 write 都會覆蓋整個 kernel buffer，不做 append
- read 支援一般檔案式的偏移前進，所以同一個 fd 讀到 EOF 後，再讀一次會得到 `0`
- 這是教學用最小模型，不是完整產品級 char driver

## 看 source code 時先抓哪幾個點

第一次讀 code 時，先把「userspace 檔案操作怎麼進 driver」串起來：

1. `driver_lab_char_init()`：如何取得 major/minor，並建立 `/dev/driver_lab_char0`
2. `dl_char_fops`：VFS 看到 `.read` / `.write` 時，會呼叫哪個 driver callback
3. `dl_char_write()`：資料怎麼從 userspace 複製進 kernel buffer
4. `dl_char_read()`：資料怎麼從 kernel buffer 複製回 userspace
5. `dl_char_lock`：為什麼 read/write 共享同一份 buffer 時需要 lock
6. `driver_lab_char_exit()`：cleanup 是否跟 init 拿資源的順序相反

先不要追 `struct inode` 或 `struct file` 的完整定義。你只需要知道它們是 VFS 傳進 callback 的上下文。

## 目前限制

- 還沒有 `ioctl`
- 還沒有 `poll`
- 還沒有 `mmap`
- buffer 固定 256 bytes

下一步是 [`../03-ioctl-poll-mmap`](../03-ioctl-poll-mmap)
