# Debug Checklist

這份清單用來排查 race、worker lifetime、safe mode 對照問題。

## 症狀：unsafe 和 safe 結果看起來差不多

先查證據：

```sh
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
```

常見原因：

- thread 數或 loop 數太低，不容易重現 lost update
- 背景 worker 也在加 counter，讓數字看起來不直覺
- 把「不穩定重現」誤解成「沒有 race」

## 症狀：`safe mode should not perform worse than unsafe mode`

先查證據：

```sh
../../tests/driver_lab_race_cli /dev/driver_lab_race0 status
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
sudo dmesg | tail -n 50
```

常見原因：

- safe mode 沒有真的切到 `1`，先用 `status` 確認。
- 兩輪實驗中間沒有 `reset`，上一輪 counter 影響觀察。
- 舊 module 或舊 CLI artifact 還在，先 `rmmod` 後重建再跑。
- race 是 timing-dependent，短時間 smoke test 偶爾可能不穩；手動加大 `race <threads> <loops>` 再觀察。
- 如果 `dmesg` 有 module load/unload 或 ioctl 錯誤，先處理 kernel log 裡的第一個錯誤。

## 症狀：`rmmod` 卡住或失敗

先查證據：

```sh
lsmod | grep '^driver_lab_race'
sudo dmesg | tail -n 50
```

常見原因：

- 背景 kthread 沒有被停掉
- 有 process 還開著 `/dev/driver_lab_race0`
- cleanup 順序和 init 拿資源順序不對稱

## 症狀：不知道該用 mutex 還是 spinlock

先查證據：

- 這段 code 是否可能在 IRQ context 執行
- 是否可能 sleep
- 共享 state 被哪些 path 讀寫

常見判斷：

- process context 且可能 sleep：先用 `mutex`
- IRQ handler 或不可 sleep path：再考慮 spinlock
- 新手先把 `mutex` 模型講清楚，不要一開始混用所有 primitive
