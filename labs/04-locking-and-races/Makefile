# obj-m 告訴 kbuild：把 driver_lab_race.c 建成 .ko 外掛 module。
obj-m += driver_lab_race.o

# KDIR 指向目前正在跑的 Linux kernel build tree。
# 外掛 module 需要借用這份 kbuild 環境，才能拿到正確的 headers 與編譯旗標。
KDIR ?= /lib/modules/$(shell uname -r)/build
# PWD 是這個 lab 自己的目錄，kbuild 會從這裡找 obj-m 與原始碼。
PWD := $(shell pwd)

.PHONY: all clean modules_install

all:
	# 建出 driver_lab_race.ko。
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
