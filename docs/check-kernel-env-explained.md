# `check-kernel-env.sh` 輸出怎麼看

## 先講結論

這個腳本不是測試你的 driver 有沒有寫對。

它只是在回答一個更前面的問題：

> 「這台 Linux 主機，有沒有基本條件可以拿來做 kernel module 練習？」

所以它的角色比較像：

- 環境健檢
- 風險預檢
- 排除最常見的起手障礙

## 你的範例輸出

```text
Kernel: 6.17.0-1010-oracle
Build tree: /lib/modules/6.17.0-1010-oracle/build
OK: found make at /usr/bin/make
OK: found gcc at /usr/bin/gcc
OK: found git at /usr/bin/git
OK: debugfs is mounted
Secure Boot state:
This system doesn't support Secure Boot
Current taint value: 0
Environment check completed.
```

## 逐行白話解釋

### `Kernel: 6.17.0-1010-oracle`

意思：

- 你現在正在跑的 Linux kernel 版本是 `6.17.0-1010-oracle`

為什麼重要：

- kernel module 一定要對著「目前正在跑的 kernel」來 build
- 不是隨便有一份 headers 就可以混用

常見對應指令：

```sh
uname -r
```

### `Build tree: /lib/modules/6.17.0-1010-oracle/build`

意思：

- 這個目錄是目前 kernel 的 build tree
- 你可以先把它想成：`外掛 module 建置時要對接的 kernel build 環境`

你剛剛問的 `KDIR` 是什麼：

- `KDIR` 只是常見變數名
- 它通常代表 `kernel directory`
- 在這個專案裡，`KDIR=/lib/modules/$(uname -r)/build`

為什麼重要：

- 外掛 module 常見建法是：

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD"
```

白話意思：

- 先切到 kernel build tree 去
- 再告訴 kbuild：「我要幫目前這個外部 module 目錄建置」

### `OK: found make at /usr/bin/make`

意思：

- 系統上有 `make`

為什麼重要：

- kbuild 是透過 `make` 驅動的

### `OK: found gcc at /usr/bin/gcc`

意思：

- 系統上有 C compiler

為什麼重要：

- kernel module 是 C 程式，要能編譯成 `.ko`

### `OK: found git at /usr/bin/git`

意思：

- 系統上有 git

為什麼重要：

- 這不是 build module 的必要條件本身
- 但對學習、版本控制、切 checkpoint commit 很重要

### `OK: debugfs is mounted`

意思：

- `debugfs` 已經掛載好了

為什麼重要：

- `01-debugfs-logging` 會在 `/sys/kernel/debug/...` 建立 debug 介面
- 如果沒掛載，你後面即使 module 載入成功，也看不到那些 debug 檔案

你現在先把 `debugfs` 想成：

- kernel 給開發者用的 debug 檔案系統
- 適合導出狀態、counter、debug knob
- 不是正式穩定 ABI

### `Secure Boot state:`
### `This system doesn't support Secure Boot`

意思：

- 這台機器目前不支援 Secure Boot，或這個環境沒有這條路徑

為什麼重要：

- 某些系統如果啟用 Secure Boot 並強制 module signing，未簽章的 `.ko` 可能會被拒絕載入
- 你這裡看到這個輸出，至少代表「不是這一條在卡你」

對你現在來說，這行的重點不是深入理解 Secure Boot，而是：

- 如果未來 `insmod` 被拒絕，Secure Boot 是一個要檢查的方向
- 但你這次的輸出沒有顯示這裡有問題

### `Current taint value: 0`

意思：

- 目前 kernel 的 taint 狀態是 `0`
- 你可以先把它理解成：`目前沒有記錄到會讓 kernel 變得“不乾淨”的標記`

為什麼重要：

- kernel 遇到某些事件時會把自己標記成 tainted
- 例如載入某些非 GPL 模組、發生某些嚴重錯誤、或其他影響 debug 信任度的狀況

對新手先記這一版就夠了：

- `0`：目前看起來是乾淨狀態
- 非 `0`：代表曾發生過某些值得注意的事件

### `Environment check completed.`

意思：

- 腳本跑完了

這不代表：

- 你的 driver 已經沒問題
- 後面一定不會失敗

它只代表：

- 最基本的環境檢查已經做完

## 對這份輸出，你現在應該得到什麼結論

以你貼的這份輸出來看，最合理的結論是：

1. 你目前確實是在 Linux 主機上
2. 目前 kernel 的 build tree 存在
3. `make`、`gcc`、`git` 都有
4. `debugfs` 已經掛載
5. 沒看到 Secure Boot / signing 是眼前障礙
6. kernel taint 目前是 `0`

所以：

> 這台機器已具備開始做 `00-hello-module` 的基本條件

## 現在你不需要過度理解的東西

先不要卡在這些名詞的深水區：

- 為什麼 `/lib/modules/.../build` 會指到那裡
- taint bitmask 每一位代表什麼
- Secure Boot 的完整簽章鏈

現在只要先知道它們各自是在回答什麼問題：

- `Kernel / Build tree`：能不能 build module
- `make / gcc / git`：工具在不在
- `debugfs`：後面 lab 能不能觀測
- `Secure Boot`：會不會擋 module 載入
- `taint`：kernel 目前是不是乾淨狀態

## 建議下一步

看完這份解釋後，直接進：

- [`../labs/00-hello-module/README.md`](../labs/00-hello-module/README.md)

不要再停在環境腳本本身打轉。
