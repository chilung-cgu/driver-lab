# Lab05 Debug Checklist

> Lab05必須在能列舉QEMU EDU的Linux guest內驗證。按「環境 → enumeration → bind → resource → MMIO → teardown」由外往內查，不要一開始就改driver source。

## 1. `lspci`不存在

```sh
command -v lspci
cat /etc/os-release
```

Debian/Ubuntu guest通常：

```sh
sudo apt update
sudo apt install -y pciutils
```

同時確認不是在macOS host或container內誤跑guest test。

## 2. 看不到`1234:11e8`

```sh
uname -m
lspci -Dnn
sudo dmesg | grep -Ei 'pci|pcie|edu'
```

先查：

- QEMU launch是否有`-device edu`；
- 登入的是不是正確guest/SSH port；
- QEMU device model是否支援EDU；
- machine arguments是否建立PCI hierarchy；
- guest boot log是否有enumeration error。

此時`probe()`根本沒有可match的`struct pci_dev`，先別改driver。

## 3. Headers或build失敗

```sh
uname -r
ls -ld "/lib/modules/$(uname -r)/build"
make V=1
```

`linux-headers-generic`已安裝不代表它對應目前running kernel。External module需要與`uname -r`匹配的build tree、generated headers與config。

## 4. Module載入但`probe()`沒進來

```sh
sudo insmod ./driver_lab_edu_mmio.ko
sudo dmesg | tail -n 120
lsmod | grep '^driver_lab_edu_mmio'
lspci -Dnnk -d 1234:11e8
find /sys/bus/pci/drivers -maxdepth 2 -type l -lname '*driver_lab_edu_mmio*' -ls
```

可能原因：

- module init/registration失敗；
- ID table不match；
- EDU已被另一driver bind；
- manual `driver_override`或policy影響binding；
- `probe()`被呼叫但很早返回，先看第一個error而不是只grep success log。

Driver先註冊或device先列舉都能bind；但function必須unbound且match/policy允許。

## 5. `pci_enable_device()`失敗

先看完整error code與：

```sh
lspci -Dnnvv -s <edu-bdf>
cat /sys/bus/pci/devices/<domain:bus:dev.fn>/enable
cat /sys/bus/pci/devices/<domain:bus:dev.fn>/resource
```

不要把所有enable failure都歸咎於BAR。可能是device state、resource conflict、platform或virtual machine configuration。

## 6. BAR type/length validation失敗

Current source在map前要求：

- BAR0含`IORESOURCE_MEM`；
- length至少覆蓋offset `0x04`加一次32-bit access。

查：

```sh
lspci -Dnnvv -s <edu-bdf>
cat /sys/bus/pci/devices/<bdf>/resource
sudo dmesg | tail -n 120
```

不要從raw config BAR自行算CPU pointer；以PCI core resource為準。

## 7. `pci_request_region()`失敗

通常表示resource已被claim或衝突。查：

```sh
cat /proc/iomem
lspci -Dnnk -s <edu-bdf>
readlink /sys/bus/pci/devices/<bdf>/driver || true
```

不要為了讓lab跑而直接移除別的driver；先確認那是不是你要測的EDU instance。

## 8. `pci_iomap()`失敗

確認：

- 前面的type、length、request都成功；
- BAR index正確；
- resource length不是0；
- architecture/platform允許該mapping；
- dmesg沒有更早的PCI resource assignment錯誤。

`pci_request_region()`成功不代表mapping一定成功；兩者是不同階段。

## 9. Identification或liveness值錯

```sh
sudo dmesg | tail -n 150
lspci -Dnnvv -s <edu-bdf>
```

核對QEMU EDU spec與current constants：

- identification offset `0x00`；
- liveness offset `0x04`；
- write pattern後應讀回bitwise inverse；
- access width為32-bit；
- mapping是byte-addressed，offset沒有被`u32 *`指標運算放大。

Liveness read-back可作先前posted write的completion point，但若值錯仍可能是offset、width、wrong device/model或mapping問題，不要機械式再疊barrier。

## 10. `test.sh`的log grep失敗

Current test**不會執行`dmesg -C`**。它記錄載入前行數、load/unload後擷取新增log；如果ring buffer在測試中wrap，會明確失敗。

查：

```sh
sudo dmesg | tail -n 200
```

可能原因：

- module已預先載入，isolated test會拒絕執行；
- `insmod`或`probe`失敗；
- log ring在測試中wrap；
- success message文字與test gate不同步；
- kernel限制非root讀`dmesg`，確認`sudo`路徑。

不要以清空共享kernel log作為修法。

## 11. `rmmod`失敗或sysfs殘留

```sh
lsmod | grep '^driver_lab_edu_mmio'
lspci -Dnnk -d 1234:11e8
ls -la /sys/bus/pci/drivers/driver_lab_edu_mmio
sudo dmesg | tail -n 120
```

Lab05沒有IRQ/DMA，但仍應確認：

```text
iounmap
→ disable device
→ release region
→ unregister PCI driver
```

若module refcount不為0，先找仍使用module的path；不要強制unload。

## 12. 何時懷疑不是Lab05 code

- EDU根本不在`lspci`；
- guest architecture/image與預期不同；
- headers不匹配；
- QEMU launch、SSH port或disk不是你以為的那台；
- 另一driver已bind；
- PCI resource assignment在boot時就失敗。

這些都應先修環境/ownership，再改source。
