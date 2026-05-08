# Linux Guest 快速檢查表：05 到 07

這份是超短版。

如果你只想知道「現在該跑什麼」，先看這張。

## 0. guest 環境

```sh
uname -a
lspci -nn | grep 1234:11e8
ls -ld /lib/modules/$(uname -r)/build
```

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
