# Debug / 測試 Playbook

## 先有觀測，再有結論

寫 driver 時，如果沒有觀測手段，幾乎一定會浪費時間。

## 第 1 層：基本 log

如果你是第一次碰 kernel module，先只用這一層就好。

### `dmesg`

```sh
sudo dmesg | tail -n 100
```

### `journalctl -k`

```sh
journalctl -k -n 100
```

適合情境：

- `insmod` 失敗
- `rmmod` 失敗
- probe 沒進來
- 明顯錯誤碼

對新手先記住：

- driver 沒反應時，第一個先看 `dmesg`
- 不要一開始就跳去懷疑很深的 race 或 DMA 問題

## 第 2 層：dynamic debug

如果模組有 `pr_debug()`：

```sh
echo 'module your_module +p' | sudo tee /proc/dynamic_debug/control
```

用途：

- 精準打開某個 module / function / line 的 debug 路徑
- 避免把系統 log 洗爆

## 第 3 層：debugfs

適合放：

- counter
- 狀態旗標
- 最後一次錯誤
- register dump
- 小型 control knob

原則：

- debugfs 是 debug 輔助，不是正式 ABI
- 正式 user API 仍應以 ioctl / read / write / poll / mmap 為主

## 第 4 層：ftrace

這份專案目前先不預設每個 lab 都用到，但在以下情況很有價值：

- 不確定 callback 有沒有被呼叫
- 想追 function 進出順序
- 想知道中斷 / workqueue / wait path 是否如預期

## 第 5 層：sanitizers 與 lock validator

### KASAN

抓：

- out-of-bounds
- use-after-free

### KCSAN

抓：

- data race

### lockdep

抓：

- 不正確的 lock ordering
- 潛在 deadlock

## 第 6 層：fault injection

至少要學會：

- `failslab`
- `fail_page_alloc`
- `fail_usercopy`

用途：

- 驗證 error path
- 驗證 rollback / cleanup
- 驗證 probe 半途失敗時是否洩漏 resource

## 每次 debug 要回答的問題

1. 問題發生前，哪個 path 被走到？
2. 你看到的是 control path 問題，還是 data path 問題？
3. 有沒有 resource 沒清？
4. 如果是 race，誰跟誰競爭？
5. 如果是 userspace 問題，kernel ABI 是否講清楚了？
