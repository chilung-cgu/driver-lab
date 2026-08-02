# 03 — ioctl / poll / record read / read-only mmap snapshot

## 目標

把Lab02的最小char device擴成四條常見driver ABI路徑：

| 路徑 | userspace | callback | 本lab用途 |
|---|---|---|---|
| data | `read/write` | `.read/.write` | 發布/消費一筆global message record |
| control | `ioctl` | `.unlocked_ioctl` | 設定message、查status、trigger/clear |
| event | `poll` | `.poll` + waitqueue | 等readable data或pending event |
| shared snapshot | `mmap` | `.mmap` | 讀kernel發布的sequenced snapshot |

這仍是teaching ABI，不是產品級multi-client queue/ring。

## 先備

- 理解`/dev`、VFS、`file_operations`；
- 完成Lab02 read/write；
- 知道userspace pointer需`copy_*_user`或相應helper；
- 能區分mutex保護kernel callbacks，不能直接鎖住userspace mmap loads。

## 介面

載入後建立：

```text
/dev/driver_lab_ctl0
```

支援：

- `write()`：發布一筆NUL-terminated internal message record；最大255 bytes。
- `read()`：blocking/nonblocking消費完整record；destination太小回`EMSGSIZE`，record保持pending。
- `DL_IOC_SET_MESSAGE`：固定256-byte struct，必須在陣列內含NUL，否則`EMSGSIZE`。
- `DL_IOC_GET_STATUS`：buffer/event狀態與實際`PAGE_SIZE`。
- `DL_IOC_TRIGGER_EVENT`：只建立event，不建立readable record。
- `DL_IOC_CLEAR_BUFFER`：清record與pending event，不重置累積event count。
- `poll()`：`POLLIN|POLLRDNORM`表示record可讀，`POLLPRI`表示event pending。
- `mmap()`：恰好一頁、read-only且non-executable snapshot；禁止後續`mprotect()`升級write/exec。

## 關鍵語意

### 1. Blocking reader醒來後仍要重新檢查

```text
wait_event_interruptible(buffer_len > 0)
→ mutex_lock
→ 再檢查buffer_len
```

另一reader可能在wait condition成立與拿lock之間先消費record。第二reader應回去睡，不是回EOF。

### 2. Read採record semantics，不使用per-open offset做跨message partial stream

舊text-buffer風格用`simple_read_from_buffer(..., ppos, ...)`會留下per-open `f_pos`。若reader A只讀一部分、reader B消費/清掉record，再發布新record，A的舊offset可能跳過新record前綴。

Current lab明確定義一筆global record：

```text
count < message length → -EMSGSIZE，record不變
count >= message length → copy整筆 → clear record/event
```

這是簡化的message device，不是POSIX byte stream。產品介面可改用per-open queue、framed UAPI或ring，但需明確設計。

### 3. Wake-up不等於`poll()`必定返回

`wake_up_interruptible()`只讓wait/poll重新評估predicate。若mask仍0，blocking poll繼續等。

| 操作 | read WQ | event WQ | readiness |
|---|---:|---:|---|
| write/ioctl-write | ✓ | ✓ | `POLLIN` + `POLLPRI`（non-empty record） |
| trigger | — | ✓ | `POLLPRI` |
| clear | — | — | ready→not-ready，不需wake |
| successful read consume | — | — | ready→not-ready，不需wake |

空字串ioctl可產生event但不產生`POLLIN`；以`POLLPRI`觀察，或clear。

### 4. Copy user data不要無必要地持有shared-state mutex

`write()`先把userspace bytes copy進stack local buffer，再取得`dl_lock`原子發布。Page fault/copy可能睡，沒必要在那段時間阻塞status/poll/ioctl。

Read則在lock內copy，因為它需要保證copy成功後才清掉同一global record。Buffer只有256 bytes，這是本lab的明確簡化；產品高吞吐設計不應長時間持lock做user copy。

### 5. Mmap是kernel→userspace snapshot

- `alloc_page()`配置normal RAM page；
- `.mmap`要求`vm_pgoff==0`與VMA恰好`PAGE_SIZE`；
- reject `VM_WRITE|VM_EXEC`；
- clear `VM_MAYWRITE|VM_MAYEXEC`，阻止`mprotect`升級；
- `vm_insert_page()`插入page；
- `VM_DONTEXPAND|VM_DONTDUMP`降低非預期VMA操作/泄漏。

Mapping size由ioctl回報，不假設所有平台4096 bytes。

### 6. Sequence snapshot

Kernel writers在`dl_lock`下：

```text
seq → odd
write fields/buffer
seq → next even
```

Userspace helper：

```text
acquire-load begin seq
→ odd則重試
→ copy snapshot
→ read fence
→ acquire-load end seq
→ begin == end且even才接受
```

Kernel mutex不會被userspace arbitrary loads取得，因此需要publication protocol。這只是固定layout snapshot，不可用來發布會被free的pointer。

### 7. UAPI字串契約

`DL_IOC_SET_MESSAGE`的`text[256]`必須含NUL。Current driver不再把未終止的256 bytes默默截成255；它回`EMSGSIZE`。這讓caller知道資料未被接受，避免「返回成功但內容被改短」。

## Source旁讀

| Source | Companion（可能需重新生成） |
|---|---|
| [`driver_lab_ioctl_poll_mmap.c`](driver_lab_ioctl_poll_mmap.c) | [`driver_lab_ioctl_poll_mmap.c.md`](driver_lab_ioctl_poll_mmap.c.md) |
| [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) | [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md) |
| [`../../runtime/src/driver_lab_runtime.c`](../../runtime/src/driver_lab_runtime.c) | [`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md) |
| [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c) | [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md) |

Companion與current source不同時，以current source與audit為準。

## 使用

```sh
make
make -C ../../runtime
sudo insmod ./driver_lab_ioctl_poll_mmap.ko

../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 ioctl-write hello-03
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 mmap-read
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 read

# terminal A
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 poll 3000
# terminal B
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 trigger

sudo rmmod driver_lab_ioctl_poll_mmap
```

## Automated test

```sh
./test.sh
```

Current regressions涵蓋：

- basic ioctl/read/status/mmap；
- writable mmap拒絕；
- read-only mapping無法`mprotect(PROT_WRITE)`；
- empty poll timeout；
- two blocking readers/one record；
- undersized read回`EMSGSIZE`且record仍可完整讀；
- unload後device/sysfs消失；
- test不卸載它沒有載入的pre-existing module。

仍建議追加：

- high-frequency writer + mmap snapshot retry；
- `mprotect(PROT_EXEC)`拒絕；
- nonblocking read empty/undersized cases；
- compat ioctl；
- KASAN/KCSAN/lockdep與repeated reload。

## 限制

- 一個global record，沒有per-open queue/fairness/backpressure。
- Reader在copy_to_user期間持mutex；只適合小teaching payload。
- 無權限模型、compat ioctl、ABI negotiation、hot-unplug。
- Sequence protocol不處理pointer lifetime。
- 真實hardware data path還有DMA ownership/cache/order/reset。

## Self-check

1. 為什麼拿mutex後要重查wait predicate？
2. 為什麼本lab拒絕partial read而回`EMSGSIZE`？
3. Wake event為何可能不讓poll返回？
4. 為什麼write先copy local再lock，read卻在lock內copy？
5. Mutex為什麼不能保證mmap reader的一致性？
6. `VM_MAYWRITE/VM_MAYEXEC`為什麼也要清？
7. 256-byte ioctl payload沒有NUL時，為什麼不應默默truncate？

<details>
<summary>參考答案</summary>

1. Predicate成立到拿lock之間可能有另一consumer改掉state；需要同一同步domain下重新判斷。
2. Global record配per-open offset會在跨reader/跨message時產生stale offset與skip；all-or-nothing明確定義message semantics並保留undersized record。
3. Wake只觸發重新評估；若readiness仍false，blocking poll繼續睡到event/timeout/signal/error。
4. Write copy不需shared state，可縮短lock hold；read若先unlock再copy，另一path可能覆蓋/清除record，所以本小buffer設計在lock內copy後才consume。
5. Userspace直接load mapping不會取得kernel mutex，需要sequence/barrier protocol辨識concurrent update。
6. 只拒絕初始`VM_WRITE/EXEC`不夠，userspace可能用`mprotect`請求後續升級；清may flags阻止。
7. 成功返回卻改變payload會隱藏資料丟失；嚴格回`EMSGSIZE`讓caller修正contract。

</details>
