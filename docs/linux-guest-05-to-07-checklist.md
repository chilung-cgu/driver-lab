# Linux Guest 快速檢查表：05 到 07

這份是給「已經照 walkthrough 跑過一次」的人用的速查單。

如果你還沒第一次進 guest，先看 [`linux-guest-05-to-07-walkthrough.md`](linux-guest-05-to-07-walkthrough.md)。

## 0. guest 環境

```sh
uname -a
lspci -nn | grep 1234:11e8
ls -ld /lib/modules/$(uname -r)/build
```

提醒：

- host 可以是 `Linux` 或 `macOS`
- 實際執行這張 checklist 的位置，一定是 `Linux guest`

## 1. repo 基本檢查

```sh
cd /path/to/driver-lab
./scripts/check-kernel-env.sh
./scripts/quality.sh .
```

## 2. 跑 05

```sh
cd /path/to/driver-lab/labs/05-pci-edu-mmio
./test.sh
```

成功重點：

- `probe start`
- `BAR0 mapped`
- `liveness check passed`

## 3. 跑 06

```sh
cd /path/to/driver-lab/labs/06-pci-edu-irq
./test.sh
```

成功重點：

- `request_irq ok`
- `irq status=...`
- `irq self-test passed`

## 4. 跑 07

```sh
cd /path/to/driver-lab/labs/07-pci-edu-dma
./test.sh
```

成功重點：

- `dma mask configured`
- `coherent buffer allocated`
- `round-trip compare passed`

## 5. 卡住時優先貼這些

```sh
sudo dmesg | tail -n 100
lspci -nn | grep 1234:11e8
```
