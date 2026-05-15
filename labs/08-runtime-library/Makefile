.PHONY: all clean

all:
	# 這一關不是 kernel module；它委派到 repo 根目錄的 runtime/ 去建 userspace CLI。
	$(MAKE) -C ../../runtime

clean:
	# 清掉 runtime 產生的 userspace CLI build artifact。
	$(MAKE) -C ../../runtime clean
