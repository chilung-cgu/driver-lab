# `launch-edu-vm.sh` 詳解

## 結論

這支script在**host**啟動一台含QEMU EDU的guest。Lab05～07的build、`insmod/rmmod`與smoke tests仍在**Linux guest**執行。

最小用法：

```sh
QEMU_IMAGE="$HOME/vm/driver-lab.qcow2" \
./qemu/launch-edu-vm.sh
```

Guest內第一個gate：

```sh
uname -m
lspci -Dnn | grep 1234:11e8
```

## Current inputs

| 變數 | 預設 | 角色 |
|---|---|---|
| `QEMU_BIN` | `qemu-system-x86_64` | System emulator binary/path |
| `QEMU_IMAGE` | 無，必填 | Guest disk image |
| `QEMU_IMAGE_FORMAT` | `qcow2` | `qcow2`或`raw`；必須符合實際image |
| `QEMU_GUEST_ARCH` | 由binary名稱推斷 | `x86_64`/`aarch64`等；wrapper binary時可覆寫 |
| `QEMU_ACCEL` | 自動 | `kvm`/`hvf`/`tcg` |
| `QEMU_EXTRA_ARGS` | 空 | Trusted local escape hatch |
| `SSH_PORT` | `2222` | Host forwarded TCP port |
| `MEMORY_MB` | `2048` | Guest RAM MiB |
| `SMP_CPUS` | `2` | Guest vCPU count |

Script會驗：

- QEMU binary存在；
- image是regular file；
- format只接受`raw`或`qcow2`；
- port是1..65535；
- RAM/vCPU為正整數；
- guest architecture可推斷或已明確指定；
- accelerator已編譯，且不是明顯的cross-ISA錯配；
- KVM還要求目前使用者可讀寫`/dev/kvm`。

## Architecture與accelerator

Script先normalize常見名稱：

```text
amd64 → x86_64
arm64 → aarch64
```

若host/guest architecture不同：

```text
arm64 host + x86_64 guest
→ default TCG
```

KVM使用host CPU virtualization extension，通常只加速相同/相容ISA；macOS HVF也不能被當成任意cross-ISA accelerator。Cross-architecture明確指定非TCG時，script拒絕並說明原因。

同架構時：

- Linux：KVM編譯存在且`/dev/kvm`可用才選KVM，否則TCG。
- macOS：QEMU列出HVF時選HVF，否則TCG。
- 其他OS：TCG。

`-accel help`只表示QEMU build列出backend；KVM還需device node/permission，因此script另外檢查。

## Guest architecture如何推斷

```text
qemu-system-x86_64 → x86_64
qemu-system-aarch64 → aarch64
```

自訂wrapper或其他binary時，設定：

```sh
QEMU_BIN=/path/to/wrapper \
QEMU_GUEST_ARCH=x86_64 \
QEMU_IMAGE=... \
./qemu/launch-edu-vm.sh
```

Binary名稱只是default inference；真正guest image/firmware/machine仍需相互匹配。

## Image format

Current launch command使用：

```sh
-drive "file=$QEMU_IMAGE,if=virtio,format=$QEMU_IMAGE_FORMAT"
```

不要把raw image宣告成qcow2或反之。先查：

```sh
qemu-img info "$QEMU_IMAGE"
```

再設定：

```sh
QEMU_IMAGE_FORMAT=raw
```

或保留`qcow2`。

Image存在並不證明其CPU architecture、boot firmware或filesystem正確；cloud image還需官方checksum與architecture核對。

## Network與EDU

Script固定加入：

```text
-netdev user,id=n1,hostfwd=tcp::<SSH_PORT>-:22
-device virtio-net-pci,netdev=n1
-device edu
-nographic
```

- User-mode network將host port轉到guest 22。
- Guest仍需SSH server/account/firewall正確。
- `-device edu`才是Lab05～07看到`1234:11e8`的來源。
- BDF由machine topology/enumeration決定，不固定。
- `-nographic`把console放在terminal。

若host port已被占用，QEMU會啟動失敗；換`SSH_PORT`。

## `QEMU_EXTRA_ARGS`

Script最後故意未quote：

```sh
# shellcheck disable=SC2086
...
$QEMU_EXTRA_ARGS
```

它允許簡單多參數：

```sh
QEMU_EXTRA_ARGS='-machine q35 -cpu qemu64'
```

限制：

- 只接受你完全信任的local input；
- 複雜nested quoting、含空白值、untrusted input不可靠；
- 複雜設定寫一支local wrapper或使用array-capable shell更安全。

Cloud-init ISO等參數也可透過它加入，但要核對script的word splitting結果。

## Host/guest責任

| Host | Guest |
|---|---|
| QEMU binary/accelerator | Running Linux kernel |
| Disk/seed/network | Matching kernel headers/build tree |
| `-device edu` | `lspci` enumeration |
| Port forwarding | Build/load/unload/tests |
| VM lifecycle | `dmesg`、sysfs、`/proc/interrupts` evidence |

Host是macOS不影響guest內可以build Linux module；但host本身不能load Linux `.ko`。

## 常見錯誤

### `QEMU_IMAGE`缺失/不存在

```sh
QEMU_IMAGE="$HOME/vm/driver-lab.qcow2" ./qemu/launch-edu-vm.sh
```

核對path與regular file。

### Format不符

```sh
qemu-img info "$QEMU_IMAGE"
QEMU_IMAGE_FORMAT=raw ...
```

### Cross-architecture卻指定KVM/HVF

```sh
QEMU_ACCEL=tcg ...
```

TCG較慢是預期，不是driver error。

### KVM列出但不可用

查：

```sh
ls -l /dev/kvm
id
```

或明確使用TCG。

### Guest看不到EDU

先確認QEMU command真的包含`-device edu`，再確認登入的是正確VM：

```sh
uname -m
lspci -Dnn
```

### SSH連不上

確認QEMU仍在跑、port未衝突、guest SSH service/cloud-init完成。

## 使用範例

### x86_64 Linux host + x86_64 guest

```sh
QEMU_IMAGE="$HOME/vm/driver-lab.qcow2" \
QEMU_IMAGE_FORMAT=qcow2 \
./qemu/launch-edu-vm.sh
```

若`/dev/kvm`可用，通常選KVM。

### arm64 host + x86_64 guest

```sh
QEMU_BIN=qemu-system-x86_64 \
QEMU_GUEST_ARCH=x86_64 \
QEMU_IMAGE="$HOME/vm/driver-lab-x86.qcow2" \
QEMU_ACCEL=tcg \
./qemu/launch-edu-vm.sh
```

詳見[`arm-host-x86-guest.md`](arm-host-x86-guest.md)。

### Raw image與自訂port

```sh
QEMU_IMAGE="$HOME/vm/guest.raw" \
QEMU_IMAGE_FORMAT=raw \
SSH_PORT=2223 \
./qemu/launch-edu-vm.sh
```

## Self-check

1. 為什麼`-accel help`列出KVM仍不代表它可用？
2. ARM host跑x86_64 guest為什麼default TCG？
3. `QEMU_IMAGE_FORMAT`錯誤會造成什麼？
4. `-device edu`與guest中的`lspci`有何關係？
5. `QEMU_EXTRA_ARGS`為什麼只能視為trusted local escape hatch？

<details>
<summary>參考答案</summary>

1. 它只反映QEMU build支援backend；KVM還需同架構與可讀寫`/dev/kvm`、host virtualization/permission可用。
2. KVM/HVF使用host硬體虛擬化，不能任意加速不同ISA；TCG做software translation。
3. QEMU會以錯誤格式解析disk，可能拒絕啟動或錯誤讀寫；先以`qemu-img info`核對。
4. QEMU建立EDU PCI function，guest PCI core列舉後才會出現`1234:11e8`供driver match。
5. POSIX shell將未quote字串做word splitting/globbing，無法安全表達任意複雜或不可信參數；複雜情境應用wrapper/array。

</details>

## 參考

- QEMU invocation: <https://www.qemu.org/docs/master/system/invocation.html>
- TCG: <https://www.qemu.org/docs/master/devel/tcg.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
