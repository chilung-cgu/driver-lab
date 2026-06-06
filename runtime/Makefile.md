# `Makefile` 詳解

## 結論

這份 Makefile 是 userspace runtime 與 CLI 的 build glue。它不建 kernel module，也不使用 kbuild；它用一般 C compiler 把 runtime implementation 和測試 CLI link 成同一個 executable：

```text
tests/driver_lab_char_cli
```

常用方式：

```sh
make -C runtime clean all
```

## 不確定處 / 查證範圍

這份講義只根據本 repo 的 `runtime/`、`tests/driver_lab_char_cli.c` 與 Lab02/Lab03 使用方式解釋。它不推測未來 runtime 是否會拆成 static library、shared library，或替 PCI EDU labs 增加專用 helper。

## 先理解這份檔案在 repo 的位置

路徑：

```text
runtime/Makefile
```

它服務的是 userspace 這一側：

- [`src/driver_lab_runtime.c`](src/driver_lab_runtime.c)：runtime function 實作。
- [`include/driver_lab_runtime.h`](include/driver_lab_runtime.h)：runtime public API。
- [`include/driver_lab_uapi.h`](include/driver_lab_uapi.h)：kernel/userspace 共用 ABI。
- [`../tests/driver_lab_char_cli.c`](../tests/driver_lab_char_cli.c)：呼叫 runtime 的 CLI。

## 這份檔案要解決什麼問題

Lab02/Lab03 的 driver 不只需要 kernel `.ko`，也需要 userspace 程式來操作 `/dev/...`：

- `open`
- `read`
- `write`
- `ioctl`
- `poll`
- `mmap`

如果每次都手動輸入 `cc ...`，很容易忘記 include path、漏掉 source，或把輸出放到不一致的位置。這份 Makefile 把「runtime + CLI」的建置固定下來。

## 讀 source 的主線

原始碼：

```make
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -Iinclude

CLI := ../tests/driver_lab_char_cli

.PHONY: all clean

all: $(CLI)

$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c

clean:
	rm -f $(CLI)
```

這份 Makefile 可以拆成四段：

1. compiler 與 flags。
2. CLI 輸出路徑。
3. `all` target。
4. build rule 與 `clean`。

## 一、`CC ?= cc`

```make
CC ?= cc
```

### 這段在做什麼

`?=` 代表「如果外部沒有設定，就使用這個預設值」。

所以預設 compiler 是：

```text
cc
```

但你可以覆寫：

```sh
make -C runtime CC=clang
```

### 為什麼 runtime 可以用一般 compiler

這裡 build 的是 userspace CLI，不是 kernel module。因此它不需要：

```sh
make -C /lib/modules/$(uname -r)/build M=...
```

那一套 kbuild 流程只屬於 kernel module。

## 二、`CFLAGS`

```make
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -Iinclude
```

### 這段在做什麼

| flag | 作用 |
|---|---|
| `-Wall` | 開啟常見 warning。 |
| `-Wextra` | 開啟更多 warning。 |
| `-Werror` | 把 warning 當成 error。 |
| `-std=c11` | 使用 C11 標準。 |
| `-Iinclude` | 讓 compiler 找到 `runtime/include` 底下的 header。 |

### 白話講

這裡把 runtime/CLI 當成正常 userspace C 程式來維護。`-Werror` 的用意是讓初學階段不要忽略 warning，因為 warning 常常就是 ABI 型別、missing prototype、unused result 之類問題的早期訊號。

## 三、輸出檔案放在 `tests/`

```make
CLI := ../tests/driver_lab_char_cli
```

### 這段在做什麼

它把產出的 executable 放到：

```text
tests/driver_lab_char_cli
```

不是放在 `runtime/` 目錄底下。

### 為什麼這樣放

`runtime/` 提供 library-like helper；`tests/` 放可執行 CLI。這樣 lab `test.sh` 可以用固定路徑呼叫 CLI，而 runtime source 仍維持在自己的目錄。

## 四、`all` target

```make
.PHONY: all clean

all: $(CLI)
```

### 這段在做什麼

`make` 預設會執行第一個 target，也就是 `all`。`all` 依賴 `$(CLI)`，所以最終會 build `../tests/driver_lab_char_cli`。

`.PHONY` 表示 `all` 和 `clean` 不是實際檔案名稱，而是命令目標。

## 五、build rule

```make
$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
```

### 這段在做什麼

當這兩個 source 比 CLI executable 新，或 executable 不存在時，make 會執行 compile/link：

```sh
cc -Wall -Wextra -Werror -std=c11 -Iinclude \
  -o ../tests/driver_lab_char_cli \
  src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
```

### `$@` 是什麼

`$@` 是 Makefile automatic variable，代表目前 target 名稱。這裡就是：

```text
../tests/driver_lab_char_cli
```

### 為什麼沒有先 build `.o`

目前 runtime 很小，只有兩個 C source。直接 compile/link 成 executable 足夠清楚。若未來 source 變多，才需要拆成 object files 或 library。

## 六、`clean`

```make
clean:
	rm -f $(CLI)
```

### 這段在做什麼

刪掉產出的 CLI executable。

### 白話講

`clean` 不刪 source、不刪 header、不刪 kernel module，只清掉 userspace build artifact。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`src/driver_lab_runtime.c`](src/driver_lab_runtime.c) | build rule 的 runtime implementation source。 |
| [`include/driver_lab_runtime.h`](include/driver_lab_runtime.h) | runtime public API header，由 `-Iinclude` 找到。 |
| [`include/driver_lab_uapi.h`](include/driver_lab_uapi.h) | ioctl command / mmap size 等 ABI 定義。 |
| [`../tests/driver_lab_char_cli.c`](../tests/driver_lab_char_cli.c) | build rule 的 CLI source。 |
| [`../labs/08-runtime-library/Makefile`](../labs/08-runtime-library/Makefile) | Lab08 wrapper，委派到這份 runtime Makefile。 |

## 常見卡點

### 為什麼 `make -C runtime` 會在 `tests/` 產生檔案？

因為 `CLI := ../tests/driver_lab_char_cli`。這是 repo 的刻意分層：runtime source 在 `runtime/`，可執行測試工具在 `tests/`。

### 為什麼這份 Makefile 不用 kbuild？

它 build userspace CLI，不是 `.ko`。kbuild Makefile 會出現在各 lab kernel module 目錄。

### 修改 header 後 make 沒有重建怎麼辦？

目前 rule 只列出兩個 `.c` dependency，沒有列 header dependency。保守做法是：

```sh
make -C runtime clean all
```

這也是 repo 驗證命令使用 `clean all` 的原因之一。

## 讀完後你應該能回答

1. 這份 Makefile build 的是 kernel module 還是 userspace executable？
2. `CC ?= cc` 和 `CFLAGS ?= ...` 為什麼方便覆寫？
3. `-Iinclude` 對 runtime headers 有什麼作用？
4. 為什麼輸出檔放在 `../tests/driver_lab_char_cli`？
5. 修改 header 後，為什麼建議跑 `make -C runtime clean all`？
