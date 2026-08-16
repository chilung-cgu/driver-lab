# `test-swiotlb.sh` 詳解

## 結論

`test-swiotlb.sh` 是 Lab07 的專用、預設不執行的 forced-SWIOTLB regression。
它不把既有 `dma_alloc_coherent()` round-trip 誤稱為 bounce-buffer coverage；它要求一個獨立、無 IOMMU 的 Linux guest 以 `swiotlb=force` 開機，再以 streaming `dma_map_single()`、kernel tracepoint 與 payload compare 同時驗證。

```text
guest cmdline: swiotlb=force
→ enable swiotlb:swiotlb_bounced trace event
→ load Lab07 with streaming_probe=1
→ map one PAGE_SIZE TX page as DMA_TO_DEVICE
→ EDU reads first 256 bytes into local RAM
→ unmap: CPU regains page ownership
→ EDU writes local RAM into coherent RX
→ compare payload and require ... size=4096 FORCE trace
```

這不是完整 streaming/SG test，也不是 IOMMU、真卡或 production-DMA claim。

`EDU_DMA_ADDRESS_BITS`（預設 `28`）決定這次要驗證的 EDU aperture，只接受
`28` 或 `32`；兩個值都必須搭配「guest 內恰好只有一顆 QEMU EDU」的獨立 fixture。

```sh
# 預設 28-bit EDU（guest RAM <= 256 MiB 的 COW）
EDU_DMA_ADDRESS_BITS=28 ./test-swiotlb.sh

# 32-bit 專用 fixture（host 以 EDU_DMA_ADDRESS_BITS=32 啟動，guest RAM 多於 256 MiB）
EDU_DMA_ADDRESS_BITS=32 ./test-swiotlb.sh
```

## 先決條件

在 disposable x86_64 Linux guest 內確認：

```sh
tr ' ' '\n' </proc/cmdline | grep -Fx 'swiotlb=force'
sudo dmesg | grep -F 'PCI-DMA: Using software bounce buffering for IO (SWIOTLB)' \
  || sudo dmesg | grep -F 'software IO TLB: '
find /sys/kernel/iommu_groups -type l -print -quit
sudo test -e /sys/kernel/tracing/events/swiotlb/swiotlb_bounced/enable \
  || sudo test -e /sys/kernel/debug/tracing/events/swiotlb/swiotlb_bounced/enable
```

IOMMU group 查詢必須沒有結果。IOMMU 與 SWIOTLB 是不同 translation path；若有 IOMMU device group，script 直接拒絕，以免把另一種 DMA mapping 機制誤報成 forced SWIOTLB。

Script 也會要求 guest 內**恰好一個** `1234:11e8`。QEMU launcher 的
`EDU_DMA_ADDRESS_BITS` 是「替換」固定那顆 EDU，不是額外加上第二顆；若
fixture 混入兩顆 EDU（例如舊參數 `-device edu` 再加 `-device edu,...`），
script 會直接拒絕，避免 bind 到錯誤的裝置或把兩顆的 trace 混在一起。

`tracefs` 可能掛在 `/sys/kernel/tracing` 或 `/sys/kernel/debug/tracing`。script 會自動選其中一個，並透過同一個 `sudo` 權限檢查 event、`trace_marker`、`trace` 與 `tracing_on`；有些 distribution 讓非 root 無法 traverse tracefs，即使檔案本身存在。若兩者都沒有完整的檔案集合，停止而不是弱化 oracle。

### 預設 28-bit EDU 的 RAM 拓撲

`swiotlb=force` 會讓 streaming map 走 SWIOTLB，但不會保證 kernel 選到的 bounce pool 位於 EDU 可定址範圍。預設 QEMU EDU 的 DMA aperture 是 28 bits（低於 256 MiB）；若在多 GiB guest 內建立 default SWIOTLB pool，pool 可以落在較高的實體位址，kernel 應回報 address overflow 並讓 `dma_map_single()` 失敗。這是正確的 DMA-mask enforcement，不是應藉由截斷 address 或提高 driver mask 掩蓋的錯誤。

因此這支 **default-EDU** regression 必須使用另一台 disposable COW，QEMU guest RAM 不超過 256 MiB，例如：

```sh
MEMORY_MB=256 QEMU_IMAGE=/path/to/disposable-swiotlb.qcow2 \
    ./qemu/launch-edu-vm.sh
```

它和一般 Lab05--07 的預設記憶體配置不同；請保存 exact QEMU command、`/proc/iomem`、kernel cmdline、source SHA 與 test log。最後仍以 driver 的 full-range check、`swiotlb_bounced ... FORCE`、payload compare 共同判斷，不能只因 guest 記憶體較小就假定 map 成功。

### Optional 32-bit fixture

若要驗證「bounce pool 高於 28-bit aperture 仍能成功」，用另一台
`swiotlb=force`、無 IOMMU 的 guest，並讓 host launcher 與 guest 測試使用同一個
`EDU_DMA_ADDRESS_BITS=32`：

```sh
EDU_DMA_ADDRESS_BITS=32 MEMORY_MB=2048 \
    QEMU_IMAGE=/path/to/disposable-swiotlb32.qcow2 \
    ./qemu/launch-edu-vm.sh
cd labs/07-pci-edu-dma
EDU_DMA_ADDRESS_BITS=32 ./test-swiotlb.sh
```

32-bit fixture 的 host 端必須是 `-device edu,dma_mask=0xffffffff`，guest 內只能
有一顆 EDU。script 會以 module parameter 回讀、`dma_mask=ffffffff` trace 與
driver log 的 mapped DMA address 高於 `0x0fffffff` 三件事共同證明：這不是把
28-bit 結果重跑一遍。

注意 `swiotlb_bounced` trace 的 `dev_addr` 是 bounce 前的原始 DMA address；
要看 bounce 後的實際位址，必須讀 driver log 的 `streaming TX map established:
... dma=0x...`。在 32-bit fixture 中，script 只要求 trace 的 `dma_mask` 相符，
高位址檢查是對 driver log 的 mapped address 做，避免把原始位址誤當成
bounce pool 證據。

## 執行

```sh
cd labs/07-pci-edu-dma
EDU_DMA_ADDRESS_BITS=28 ./test-swiotlb.sh   # 預設 EDU
EDU_DMA_ADDRESS_BITS=32 ./test-swiotlb.sh   # 32-bit 專用 fixture
```

script 會拒絕已載入的 `driver_lab_edu_dma`，只在自己成功 `insmod` 後才負責 `rmmod`。它也會保存與恢復原本的 trace event enable 與 `tracing_on` 狀態，避免永久改掉 guest 的 trace 設定。

## 三組必須同時成立的證據

| 證據 | script 如何檢查 | 能支持什麼 | 不能支持什麼 |
|---|---|---|---|
| Boot mode | exact `swiotlb=force` token、SWIOTLB boot log、無 IOMMU group | 測試跑在指定 forced-SWIOTLB guest | 任何特定 DMA transaction 已成功 |
| Kernel mechanism | marker 切出的 `swiotlb:swiotlb_bounced` event，包含 EDU BDF、`size=4096 FORCE`，且 `dma_mask` 與 fixture 一致（28-bit 為 `fffffff`、32-bit 為 `ffffffff`） | kernel 為此 streaming map 走到 SWIOTLB bounce tracepoint，且 driver/kernel mask 對應到 fixture | map 一定成功或 payload 一定正確 |
| 32-bit 專用檢查 | 32-bit 模式額外要求 driver log 的 `streaming TX map established` DMA address 高於 `0x0fffffff` | 這次 run 真的用到 28-bit 以上的 bounce 位址 | 只有一個高位址就代表所有 DMA 都安全 |
| Driver result | map success、兩次 transfer、`streaming-to-EDU-to-coherent-RX compare passed`、無 DMA-API/quiesce warning | EDU 接受 mapped DMA address，256-byte payload 正確且 teardown 未見已知失敗訊號 | SG、長壽命 mapping、真卡 cache/firmware/recovery 行為 |

三者缺任何一個都算失敗。特別是 `dma_need_sync=1` 只記錄為固定環境的交叉檢查；DMA ops、IOMMU 或 non-coherent platform 也可能需要 sync，不能單獨證明 SWIOTLB bounce。

## 為何 mapping 是整頁、transfer 只有 256 bytes

driver 以 `get_zeroed_page()` 配一整頁，並對整個 `PAGE_SIZE` 執行：

```c
dma_map_single(dev, stream_tx_buf, PAGE_SIZE, DMA_TO_DEVICE)
```

這避免 partial cache-line 規則掩蓋本教學的主要問題。QEMU EDU 現有 command 每次只搬 `DL_EDU_DMA_BUFFER_BYTES`（256 bytes），因此只讀 mapping 的前 256 bytes。Map 之後到 `dma_unmap_single()` 之前 CPU 不碰 TX page；unmap 後才和 coherent RX 比較。

若 command timeout，driver 不會立刻 unmap/free。它沿用 Lab07 的 quiesce flow：先停 BME、確認 EDU command idle 或嘗試 reset；只有確認沒有延遲 DMA 時才交還 mapping/page。無法證明時，故意 retain mapping，比 DMA use-after-free 安全。

## Trace window 與 failure handling

script 不清全域 trace buffer。它寫入 begin/end `trace_marker`，只擷取兩個 marker 間的 event，避免把舊的 SWIOTLB event 當成本輪證據。

它也不把「看到 trace」當成單一成功訊號：source 仍檢查 `dma_mapping_error()`，script 要求 payload compare、device unbind 與沒有下列 marker：

```text
DMA-API:
BUG:
WARNING:
KASAN:
KCSAN:
cannot prove DMA quiescence
retaining coherent mapping
streaming mapping intentionally retained
```

## 常見卡點

- 缺少 `swiotlb=force`：請修改隔離 guest 的 boot entry 後重開機；不能在已開機的 kernel 上假裝已 force。
- 找到 IOMMU group：換用無 IOMMU 的 clone；不要刪除這個 guard。
- 沒有 tracefs event：確認 kernel config/tracefs mount；不能以 `dma_need_sync` 替代。
- 出現 `swiotlb addr ... overflow` 或 streaming map failed：先比對 bounce address 與 EDU 28-bit mask；換成新的 <=256 MiB default-EDU COW，保留失敗 log，不要截斷 address、移除 range check 或把這個 fixture 偷換成 IOMMU。
- `EDU_DMA_ADDRESS_BITS` 不是 28/32：script 直接拒絕；driver 也只接受這兩個值，其他值會在 probe 回傳 `-EINVAL`。
- 32-bit 模式看到低於 `0x0fffffff` 的 mapped DMA address：調整 guest RAM/拓撲，讓 SWIOTLB pool 落在 256 MiB 以上；不要降低檢查標準，也不要拿 trace 的 `dev_addr`（原始位址）冒充 bounce 位址。
- module parameter 回讀與 `EDU_DMA_ADDRESS_BITS` 不符：先確認 host launcher 的 `edu,dma_mask`、guest cmdline 與 `insmod` 參數一致，再談其他證據。
- `FORCE` trace 缺失：先確認 EDU BDF、page size、module parameter 與 trace window；也要保留 raw trace 作故障證據。
- compare 或 timeout 失敗：保留 dmesg、trace window、QEMU version/device/boot config、source SHA 與 exact command；不要只重試直到通過。

## 查證來源

- Linux DMA API HOWTO：streaming mapping 的 address/ownership/同步語意。<https://docs.kernel.org/core-api/dma-api-howto.html>
- Linux source `swiotlb_bounced` trace event：`dev_name`、`size` 與 `FORCE` field。<https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/trace/events/swiotlb.h?h=v6.8>
- QEMU EDU device：DMA command、local RAM aperture 與 28-bit DMA model。<https://www.qemu.org/docs/master/specs/edu.html>
