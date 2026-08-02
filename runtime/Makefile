CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -Iinclude

CLI := ../tests/driver_lab_char_cli
UNIT := ../tests/driver_lab_runtime_unit

.PHONY: all test clean

all: $(CLI)

$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c \
	include/driver_lab_runtime.h include/driver_lab_uapi.h
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c

$(UNIT): src/driver_lab_runtime.c ../tests/driver_lab_runtime_unit.c \
	include/driver_lab_runtime.h include/driver_lab_uapi.h
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_runtime_unit.c

test: $(UNIT)
	$(UNIT)

clean:
	rm -f $(CLI) $(UNIT)
