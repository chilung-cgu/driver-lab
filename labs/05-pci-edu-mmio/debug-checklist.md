# Debug Checklist

- PCI device ID 是否正確？
- `probe` 是否真的被呼叫？
- `pci_enable_device()` 是否成功？
- BAR size / type 是否如預期？
- `pci_iomap()` 回傳值是否有效？
- read identification / liveness path 是否可觀測？

