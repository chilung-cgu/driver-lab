obj-m += driver_lab_edu_irq.o

# KDIR 指向目前正在跑的 Linux kernel build tree。
# 外掛 module 會借用這份 kbuild 環境來取得正確的 headers 與編譯旗標。
KDIR ?= /lib/modules/$(shell uname -r)/build
# PWD 是這個 lab 自己的目錄，kbuild 會從這裡找 obj-m 與原始碼。
PWD := $(shell pwd)

.PHONY: all clean modules_install

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
