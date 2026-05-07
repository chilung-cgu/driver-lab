# 00 - Hello Module

## 目的

建立最小閉環：

- build
- load
- 看 log
- unload
- clean

如果這一關做不穩，後面的 driver lab 都會浪費時間。

## 你會學到什麼

- kbuild 的最小外掛模組寫法
- `module_init` / `module_exit`
- module parameter
- `dmesg` 觀測
- `insmod` / `rmmod`

## 先備理解

先用白話記住這 3 件事：

- `insmod` 載入模組時，kernel 會呼叫 `module_init()` 指定的函式
- `rmmod` 卸載模組時，kernel 會呼叫 `module_exit()` 指定的函式
- 這不是一般 userspace 程式，所以最重要的觀測點是 `dmesg`

## 你現在在系統的哪一層

這一關還沒有 `/dev`、沒有 user-kernel data path，也沒有硬體。

你只是在練：

- kernel module 能不能被正確 build
- module 能不能被載入
- module 的 init / exit path 能不能被觀測

## 檔案

- `driver_lab_hello.c`
- `Makefile`
- `test.sh`
- `quality.sh`

## 使用方式

```sh
make
modinfo ./driver_lab_hello.ko
sudo insmod ./driver_lab_hello.ko who=linux repeat=2
lsmod | grep '^driver_lab_hello'
sudo dmesg | tail -n 20
sudo rmmod driver_lab_hello
make clean
```

命令逐行在做什麼：

- `make`：用目前目錄的 `Makefile` 建出 `driver_lab_hello.ko`
- `modinfo`：先看這個 module 的基本資訊，確認檔案有被正確產生
- `insmod ... who=linux repeat=2`：把 module 載入 kernel，並帶入參數
- `lsmod | grep ...`：確認 module 真的在 kernel 裡
- `dmesg | tail ...`：看剛剛載入時印出的 kernel log
- `rmmod`：卸載 module
- `make clean`：刪掉建置產物

## 自動化 smoke test

```sh
./test.sh
```

## 驗收標準

- `make` 成功
- `insmod` 成功
- kernel log 出現 `driver_lab_hello`
- `rmmod` 成功
- `make clean` 成功

## 常見失敗

- `/lib/modules/$(uname -r)/build` 不存在
- Secure Boot / module signing 導致載入失敗
- 不是在 Linux 主機上執行

## 看不懂 code 時，至少先找到這 3 個位置

- `driver_lab_hello_init()`：模組載入時做什麼
- `driver_lab_hello_exit()`：模組卸載時做什麼
- `module_param()`：參數是怎麼進來的

## 下一步

完成後進入 [`../01-debugfs-logging`](../01-debugfs-logging)
