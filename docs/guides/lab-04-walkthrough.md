# 04 實作導讀：Locking and Races

這份文件不是 API 手冊，而是給第一次接觸 race 的人看的「白話導讀版」。

如果你還沒做過 [`../../labs/03-ioctl-poll-mmap`](../../labs/03-ioctl-poll-mmap)，先不要直接跳進來。

## 這一關到底在做什麼

你前面幾關主要在學：

- module 怎麼載入與卸載
- userspace 怎麼呼叫 driver
- `ioctl` / `poll` / `mmap` 是什麼

這一關第一次把焦點放在：

- 同一份 kernel state 被很多路徑同時碰時會怎樣

最小情境是：

- 背景 worker thread 一直在加 counter
- userspace 也一直透過 `ioctl` 加 counter
- 如果沒有同步機制，就會出現 lost update

## 這個 lab 刻意設計的兩種模式

### 模式 1：`safe_mode = 0`

這是故意做壞的版本。

counter 增加流程被拆成：

1. 先讀目前值
2. 故意睡一下
3. 再把 `snapshot + 1` 寫回去

如果很多 thread 同時做這件事，就可能：

- thread A 讀到 `100`
- thread B 也讀到 `100`
- A 寫回 `101`
- B 也寫回 `101`

結果本來應該加 2，最後只加 1。

### 模式 2：`safe_mode = 1`

這是修正後版本。

每次加 counter 前先拿 `mutex`，做完再放掉。

這樣同一時間只會有一條路徑真的修改 counter。

## 你要先看哪幾個點

第一次讀 [`../../labs/04-locking-and-races/driver_lab_race.c`](../../labs/04-locking-and-races/driver_lab_race.c) 時，只抓這幾段：

1. `dl_counter`
   - 共享資料本體
2. `dl_safe_mode`
   - 決定目前走 unsafe 還是 safe 路徑
3. `dl_race_increment_unlocked()`
   - 故意示範 race 的核心
4. `dl_race_increment()`
   - safe / unsafe 模式切換點
5. `dl_race_worker_fn()`
   - 背景 thread，不需要 userspace 指令也會一直動
6. `dl_race_ioctl()`
   - userspace 操作進到 kernel 的入口

## userspace 測試工具在做什麼

[`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c) 的重點不是漂亮，而是單純把 race 踩出來。

你第一次只要理解兩件事：

1. `race <threads> <loops>`
   - 建立多條 userspace thread
   - 每條 thread 重複送 `DL_RACE_IOC_INC_COUNTER`
2. 最後再用 `DL_RACE_IOC_GET_STATUS`
   - 把目前 counter 值讀回來

## 為什麼會看到 `expected_at_least`

背景 worker 也會一直加 counter。

所以 userspace 自己送的 `threads * loops` 只是「至少應該有這麼多」。

實際值通常會：

- unsafe 模式：小於預期很多，因為 lost update 明顯
- safe 模式：大於等於預期，因為 userspace increment 不再彼此覆蓋，而且 worker 還會額外增加

## 第一次跑這一關的順序

1. `safe-mode 0`
2. `reset`
3. `race 8 50`
4. 看 `observed`
5. `safe-mode 1`
6. `reset`
7. `race 8 50`
8. 再看一次 `observed`

你不用期待每次數字都完全一樣。

這一關的重點是：

- unsafe 模式的結果通常比較差
- safe 模式通常更接近合理值

## 第一次驗收時只問自己這三題

| 問題 | 標準答案 |
|---|---|
| 共享資料是誰？ | 第一輪先回答 `dl_counter`；延伸來看，`dl_safe_mode` 和 worker 狀態也是共享 state。 |
| 哪些路徑會碰到它？ | 背景 worker thread 會碰，userspace 透過 `ioctl` 也會碰；`race <threads> <loops>` 會讓多個 userspace thread 同時施壓。 |
| race 是怎麼被修掉的？ | safe mode 用 `mutex` 序列化 `dl_counter` 的 increment，避免多條路徑同時 read-modify-write。 |

## 常見誤解

### 為什麼不用 `spinlock`？

因為這一關故意在 unsafe 路徑裡睡眠。

如果你要保護一段可能睡眠的路徑，第一個該想的是 `mutex`，不是 `spinlock`。

### 為什麼 `observed` 可能比 `expected_at_least` 還大？

因為背景 worker 同時也在加 counter。

### 為什麼不要求 safe 模式一定等於某個精確值？

因為這不是純 userspace 單執行緒測試。

只要背景 worker 還在跑，值就會持續變動，所以第一次驗收看的是「unsafe 比較差，safe 比較穩」。

## 下一步學什麼

這一關做熟後，再去碰：

- waitqueue
- completion
- lockdep
- KCSAN

那時候你就不是只會背名詞，而是知道它們在解哪一類問題。
