c_src := $(shell find ../../src/ -type f -name '*.c')
c_obj := $(c_src:.c=.o)
CC := i686-elf-gcc
LD := i686-elf-gcc
CFLAGS := -nostdlib -fno-builtin -fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -c -mno-sse -mno-sse2 -mno-sse3 -mno-mmx  -I../../../../userlib/include -mno-sse4 -mno-sse4.1 -mno-sse4.2 -fno-pic -ffreestanding -fstrict-volatile-bitfields -g
LDFLAGS := -ffreestanding -static -nostdlib -no-pie

all: build clean

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

build: $(c_obj) $(asm_obj)
	$(LD) $(LDFLAGS) $^ ../../../../userlib/build/i686/libcpl1.a -o ls -lgcc

.PHONY: clean run run_with_ints debug
clean:
	rm -f $(c_obj)
	rm -f $(asm_obj)
	rm -f ls

