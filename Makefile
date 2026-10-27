# Bare-metal playground: freestanding RV32I with no C library and no libgcc.
#
# Build inside the rv32emu tree (tests/system/playground) so that
# toolchain.mk can locate the cross compiler.
#
#   make              build test.elf at $(OPT)
#   make run          execute test.elf on the emulator
#   make dump         page through the disassembly
#   make clean && make OPT=-O2
#                     rebuild at another optimization level
#
# The cycle and instruction counts printed by the program depend on OPT;
# the defaults reproduce the figures quoted in the lab handout. The Makefile
# does not track flag changes, so run make clean before changing OPT.

include ../rv32emu/mk/toolchain.mk

EMU ?= ../rv32emu/build/rv32emu
OPT ?= -O0

# The base integer ISA plus Zicsr, which the counters need (csrr).
ARCH = -march=rv32i_zicsr -mabi=ilp32
LINKER_SCRIPT = linker.ld

AFLAGS = -g $(ARCH)
CFLAGS = -g $(OPT) $(ARCH) -Wall -Wextra
# The image merges code and data into one segment; silence the RWX warning.
LDFLAGS = -T $(LINKER_SCRIPT) --no-warn-rwx-segments
EXEC = test.elf

CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJDUMP = $(CROSS_COMPILE)objdump

OBJS = start.o main.o perfcounter.o chacha20_asm.o

.PHONY: all run dump clean

all: $(EXEC)

$(EXEC): $(OBJS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.S
	$(AS) $(AFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@ -c

run: $(EXEC)
	@test -f $(EMU) || (echo "Error: $(EMU) not found" && exit 1)
	$(EMU) $<

dump: $(EXEC)
	$(OBJDUMP) -Ds $< | less

clean:
	rm -f $(EXEC) $(OBJS)
