# Debug Checklist

- DMA mask 是否真的設成功？
- 分到的是 coherent 還是 streaming buffer？
- source / destination / count register 是否正確？
- timeout 時是否能安全回收？
- 中斷完成與 polling 完成兩條 path 是否一致？

