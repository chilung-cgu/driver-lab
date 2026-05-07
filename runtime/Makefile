CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -Iinclude

CLI := ../tests/driver_lab_char_cli

.PHONY: all clean

all: $(CLI)

$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c

clean:
	rm -f $(CLI)

