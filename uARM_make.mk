# Cross toolchain variables
# If these are not in your path, you can make them absolute.
XT_PRG_PREFIX = arm-none-eabi-
CC = $(XT_PRG_PREFIX)gcc
LD = $(XT_PRG_PREFIX)ld

# uARM-related paths
UARM_DIR = ./include/uARM
DEVICE_DIR = ./device
PROCESS_DIR = ./process
INCLUDE_DIR = ./include

# Compiler options
CFLAGS_LANG = 
CFLAGS_UARM = -mcpu=arm7tdmi -DTARGET_UARM=1
CFLAGS = $(CFLAGS_LANG) $(CFLAGS_UARM) -I$(DEVICE_DIR) -I$(PROCESS_DIR) -I$(UARM_DIR) -I$(UARM_DIR)/uarm -I$(INCLUDE_DIR) -DUARM=1 -Wall -O0

# Linker options
LDFLAGS = -G 0 -nostdlib -T $(UARM_DIR)/elf32ltsarm.h.uarmcore.x

# Add the location of crt*.S to the search path
VPATH = $(UARM_DIR)

.PHONY : all clean

all : kernel.core.uarm

kernel.core.uarm : kernel
	elf2uarm -k $<

kernel : phase1,5_test.o device/term_utils.o device/printer_utils.o process/pcb.o process/asl.o crtso.o libuarm.o
	$(LD) -o $@ $^ $(LDFLAGS)

clean:
	-rm -rf *.o kernel kernel.*.uarm
	-rm $(DEVICE_DIR)/*.o $(PROCESS_DIR)/*.o

# Pattern rule for assembly modules
%.o : %.s
	$(CC) $(CFLAGS) -c -o $@ $<
