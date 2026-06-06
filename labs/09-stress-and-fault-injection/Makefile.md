# `Makefile` 詳解

## 結論

`labs/09-stress-and-fault-injection/Makefile` 目前是 scaffold。Lab09 的實際工作在 shell scripts：

```text
test.sh
stress-03-reload.sh
stress-03-parallel.sh
```

所以 `make` 只印提示訊息，不 build `.ko`、不 build CLI、也不跑 stress。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`Makefile`](Makefile) 本身。
- Lab09 runner：[`test.sh.md`](test.sh.md)。
- 兩支 stress scripts：[`stress-03-reload.sh.md`](stress-03-reload.sh.md)、[`stress-03-parallel.sh.md`](stress-03-parallel.sh.md)。

這裡不把 Makefile 說成測試框架；它目前只是保留未來新增 stress tools / fault injection scripts 的入口。

## 一、`.PHONY`

原始碼：

```make
.PHONY: all clean
```

`all` 和 `clean` 是命令目標，不是實際檔案。

## 二、`all`：目前只印 scaffold 訊息

原始碼：

```make
all:
	# 這一關目前主要靠 stress shell scripts；Makefile 先保留 scaffold 訊息。
	@printf '%s\n' "Scaffold only. Add stress tools and fault injection scripts first."
```

`@` 讓 make 不印出 command 本身，只印 `printf` 的結果。

目前執行：

```sh
make
```

只會看到：

```text
Scaffold only. Add stress tools and fault injection scripts first.
```

真正 stress suite 請跑：

```sh
./test.sh
```

## 三、`clean`：目前沒有本目錄 build artifact

原始碼：

```make
clean:
	# 目前沒有本目錄專屬 build artifact。
	@:
```

`:` 是 shell no-op。意思是 clean target 成功，但不做任何事。

這符合目前狀態：

```text
Lab09 沒有本目錄專屬 binary
Lab09 沒有本目錄專屬 .ko
Lab09 scripts 會各自 build/clean 目標 lab
```

## 和 test/stress scripts 的對照

| 入口 | 目前作用 |
|---|---|
| `make` | 印 scaffold 訊息 |
| `make clean` | no-op |
| `./test.sh` | 串起 Lab03 reload + parallel stress |
| `./stress-03-reload.sh` | 直接跑 repeated load/unload |
| `./stress-03-parallel.sh` | 直接跑 parallel userspace access |

## 常見卡點

- 以為 `make` 會跑 stress：目前不會。
- 以為 `make clean` 會清 Lab03 artifacts：目前不會；stress scripts 自己 cleanup。
- 要驗 Lab09 目前能力：跑 `./test.sh`。
- 未來若新增 compiled stress tool，才需要擴充這份 Makefile。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab09 Makefile 目前會 build 什麼？ | 什麼都不 build，只印 scaffold 訊息。 |
| Lab09 真正 stress 入口是什麼？ | `./test.sh`。 |
| `make clean` 目前做什麼？ | no-op。 |
| 為什麼仍保留 Makefile？ | 作為未來新增 stress tools / fault injection scripts 的標準入口。 |
