# obj-m 告訴 kbuild：把 driver_lab_debugfs_logging.c 建成 .ko 外掛 module。
obj-m += driver_lab_debugfs_logging.o

# KDIR 指向目前 Linux kernel 的 build tree；外掛 module 必須透過 kbuild 建置。
KDIR ?= /lib/modules/$(shell uname -r)/build
# PWD 是這個 lab 目錄；M=$(PWD) 讓 kbuild 回到這裡找 source 與 obj-m。
PWD := $(shell pwd)

.PHONY: all clean modules_install

all:
	# 建出 driver_lab_debugfs_logging.ko。
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	# 清掉 kbuild 產生的暫存檔與 .ko。
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	# 保留標準 kbuild 目標；一般學習流程不需要執行。
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
