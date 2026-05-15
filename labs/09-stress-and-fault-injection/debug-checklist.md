# Debug Checklist

這一關目前已有 `03-ioctl-poll-mmap` 專用 stress 腳本，還不是完整 fault injection framework。

## 症狀：repeated load/unload 偶發失敗

先查證據：

```sh
./stress-03-reload.sh
sudo dmesg | tail -n 120
```

常見原因：

- init/exit cleanup 不對稱
- 前一次測試留下 module 或 device node
- 有 process 尚未關閉 device fd

## 症狀：parallel access 測試失敗

先查證據：

```sh
./stress-03-parallel.sh
sudo dmesg | tail -n 120
```

常見原因：

- shared state lock 不完整
- event / buffer / mmap page 沒有一致更新
- CLI 或 runtime 在壓力下暴露錯誤處理不足

## 症狀：不知道 stress 和 regression 差在哪

先記住：

- stress：刻意重複施壓，增加問題出現機率
- regression：每次修改後固定重跑，避免舊功能壞掉
- fault injection：主動讓配置、copy、IRQ、DMA 等路徑失敗，檢查 cleanup 是否正確
