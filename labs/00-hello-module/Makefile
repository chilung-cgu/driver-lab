# obj-m 告訴 kbuild：把 driver_lab_hello.c 建成外掛 module driver_lab_hello.ko。
obj-m += driver_lab_hello.o

# KDIR 指向「目前正在跑的 Linux kernel」對應的 build tree。
# 外掛 module 需要借用 kernel 的 kbuild，不能直接用一般 gcc 指令編。
KDIR ?= /lib/modules/$(shell uname -r)/build
# PWD 是這個 lab 目錄；M=$(PWD) 會告訴 kbuild 外掛 module source 在這裡。
PWD := $(shell pwd)

.PHONY: all clean modules_install

all:
	# 進入 kernel build tree，回頭建目前目錄裡的 obj-m module。
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	# 清掉 kbuild 產生的 .ko、.o、Module.symvers、modules.order 等檔案。
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	# 教學 lab 通常不需要安裝到系統 module 目錄；保留作為 kbuild 標準目標。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
