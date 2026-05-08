# Debug Checklist

- 先確認 `05` 與 `06` 都穩定，再來看 DMA。
- DMA mask 是否真的設成功？
- 分到的是 coherent 還是 streaming buffer？
- source / destination / count register 是否正確？
- timeout 時是否能安全回收？
- 中斷完成與 polling 完成兩條 path 是否一致？
- `dmesg` 裡是否真的看到 `round-trip compare passed`？
