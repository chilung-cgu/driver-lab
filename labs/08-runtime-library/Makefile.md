# `Makefile` 詳解

## 結論

`labs/08-runtime-library/Makefile` 不是 kernel module kbuild。它是 Lab08 的 userspace runtime build 入口，實際工作委派給 repo 根目錄的 [`../../runtime/Makefile`](../../runtime/Makefile)：

```text
labs/08-runtime-library/Makefile
  -> make -C ../../runtime
  -> build tests/driver_lab_char_cli
```

所以這一關不會產生 `.ko`，也不會 `insmod`。它驗的是 userspace runtime 與 CLI build glue。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab08 test：[`test.sh.md`](test.sh.md)。
- runtime build file：[`../../runtime/Makefile`](../../runtime/Makefile)。
- runtime source 旁讀：[`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md)、[`../../runtime/include/driver_lab_runtime.h.md`](../../runtime/include/driver_lab_runtime.h.md)、[`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md)。

這裡只解釋 Lab08 的委派式 Makefile，不重新講整份 runtime source；runtime `.c/.h` 已有相鄰 companion docs。

## 一、`.PHONY`

原始碼：

```make
.PHONY: all clean
```

`all` 和 `clean` 是命令目標，不是實際要產生的檔名。

第一輪只要知道：

```text
make
  -> 跑 all

make clean
  -> 跑 clean
```

## 二、`all`：委派到 `runtime/`

原始碼：

```make
all:
	# 這一關不是 kernel module；它委派到 repo 根目錄的 runtime/ 去建 userspace CLI。
	$(MAKE) -C ../../runtime
```

這裡的重點是 `$(MAKE) -C ../../runtime`。

展開成概念流程：

```text
目前目錄：labs/08-runtime-library
  -> 進入 ../../runtime
  -> 執行 runtime/Makefile 的預設 target
```

runtime build 的主要產物是：

```text
tests/driver_lab_char_cli
```

它是 userspace CLI binary，不是 kernel module。

## 三、`clean`：委派 runtime cleanup

原始碼：

```make
clean:
	# 清掉 runtime 產生的 userspace CLI build artifact。
	$(MAKE) -C ../../runtime clean
```

這會呼叫 [`../../runtime/Makefile`](../../runtime/Makefile) 的 `clean` target，清掉 runtime 產生的 CLI build artifact。

重要觀念：

```text
make clean 不會 rmmod
make clean 不會刪 /dev node
make clean 不會驗證 kernel driver 行為
```

Lab08 是 userspace runtime/library build 檢查。真正 driver 行為仍回到 Lab02/Lab03 的 smoke test。

## 和 runtime/test 的對照

| Lab08 target | 實際動作 | 後續誰檢查 |
|---|---|---|
| `all` | `make -C ../../runtime` | [`test.sh`](test.sh) 檢查 CLI binary 存在且可執行 |
| `clean` | `make -C ../../runtime clean` | 手動 cleanup 或其他 test 收尾 |

## 常見卡點

- 看到 Lab08 Makefile 沒有 `obj-m` 是正常的；它不是 kernel module。
- build 成功只代表 runtime/CLI 編譯成功，不代表 `/dev/driver_lab_*` 存在。
- 如果 build 失敗，先看 [`../../runtime/Makefile`](../../runtime/Makefile) 的 compile command 和 include path。
- 如果想驗 runtime 實際呼叫 driver，回 Lab02/Lab03 的 Linux smoke test。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab08 Makefile 會建 `.ko` 嗎？ | 不會；它委派到 `runtime/` 建 userspace CLI。 |
| `make -C ../../runtime` 是什麼意思？ | 切到 repo 根目錄的 `runtime/` 目錄執行那邊的 Makefile。 |
| Lab08 主要 build artifact 是什麼？ | `tests/driver_lab_char_cli`。 |
| `make clean` 會卸載 driver 嗎？ | 不會；它只清 userspace build artifact。 |
| 真正 driver 行為要在哪裡驗？ | Lab02/Lab03 的 Linux smoke test。 |
