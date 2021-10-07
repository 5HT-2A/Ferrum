ARCH ?= x86

QEMU = qemu-system-i386

# Tools.

# If we need Floating Point instructions, build Atom with i486+
ifdef TRUEX86
CC = i686-elf-gcc
LD = i686-elf-gcc
AS = i686-elf-gcc
endif

# Else, stick with the first bare-metal x86-32 iteration (i386, protected mode and paging)
CC = i386-elf-gcc
LD = i386-elf-gcc
AS = i386-elf-as

# Check that we have the required software.
ifeq (, $(shell which $(CC)))
    $(error "$(CC) not found. Is the toolchain compiler enabled?")
endif
ifeq (, $(shell which $(LD)))
    $(error "$(LD) not found. Is the toolchain compiler enabled?")
endif
ifeq (, $(shell which $(AS)))
    $(error "$(AS) not found. Is the toolchain compiler enabled?")
endif

QEMUARGS =
QEMU_DEBUGARGS = -s -S & gdb --eval-command="target remote localhost:1234"

BOCHS = bochs

ifdef USE_FLOPPY
# 1.44 MB Floppy for now
MEDIA = floppy.img
# Looks like DD considers floppy's physical Sector 1 as 0, use this variable to avoid confusion.
FLOPPY_SECTOR1 = 0
# Do the same for Sector 2, then, we will calculate the size of the second stage bootloader (boot1)
# and dynamically assign the starting (Kernel-containing) sector using that.
FLOPPY_SECTOR2 = 1
# Hardcoded to 3 for now! Find a way to find this variable dynamically using Second Stage Bootloader's
# (boot1) size.
FLOPPY_KERNEL_STARTING_SECTOR = 6
endif

HDD_MBR_SECTOR = 0
HDD_SECOND_STAGE_SECTOR = 1
HDD_KERNEL_STARTING_SECTOR = 4

LDSCRIPT = kernel/arch/$(ARCH)/linker.ld

ifdef I_FS_FAT16
LDFLAGS = -Wl,-DI_FS_FAT16=1
endif

ifdef I_FS_NONE
LDFLAGS = -Wl,-DI_FS_NONE=1
endif

.PHONY: clean bochs qemu qemu-debug

all: clean hdd.img

floppy.img: arch bloader
	-@mkfs.msdos -C floppy.img 1440 >/dev/null
	-@dd conv=notrunc if=bootloader/$(ARCH)/boot0.bin of=floppy.img bs=512 seek=$(FLOPPY_SECTOR1) status=none
	-@dd conv=notrunc if=bootloader/$(ARCH)/boot1.bin of=floppy.img bs=512 seek=$(FLOPPY_SECTOR2) status=none
	-@dd conv=notrunc if=kernel/arch/$(ARCH)/kernel.bin of=floppy.img bs=512 seek=$(FLOPPY_KERNEL_STARTING_SECTOR) status=none

ifdef I_FS_FAT16
hdd.img: arch bloader
	-@bximage -mode=create -hd=10M -q hdd.img >/dev/null # 10485760 Bytes = 10 Mega Bytes
	-@mkfs.fat -F 16 hdd.img -v
	-@mkdir disk/
	-@dd conv=notrunc if=bootloader/$(ARCH)/boot0.bin of=hdd.img bs=512 seek=$(HDD_MBR_SECTOR) status=none
	-@sudo mount -t msdos -o loop hdd.img disk/
	-@sudo cp bootloader/$(ARCH)/boot1.bin disk/STAGE2
	-@sudo umount disk/
	-@rm -r disk/
endif

ifdef I_FS_NONE
hdd.img: arch bloader
	-@bximage -mode=create -hd=16M -q hdd.img >/dev/null # 16777216 Bytes = 16 Mega Bytes
	-@dd conv=notrunc if=bootloader/$(ARCH)/boot0.bin of=hdd.img bs=512 seek=$(HDD_MBR_SECTOR) status=none
	-@dd conv=notrunc if=bootloader/$(ARCH)/boot1.bin of=hdd.img bs=512 seek=$(HDD_SECOND_STAGE_SECTOR) status=none
	-@dd conv=notrunc if=kernel/arch/$(ARCH)/kernel.bin of=hdd.img bs=512 seek=$(HDD_KERNEL_STARTING_SECTOR) status=none
endif

arch:
	make -C kernel/arch/$(ARCH)

bloader:
	make -C bootloader/$(ARCH)

clean:
	make -C kernel/arch/$(ARCH) clean
	make -C bootloader/$(ARCH) clean
	-@rm hdd.img

ifdef I_FS_FAT16
bochs:
	$(BOCHS) -q -f bochsrc.bxrc 'ata0-master: type=disk, path=hdd.img, mode=flat, cylinders=20, heads=16, spt=63, sect_size=512, model="Generic 1234", biosdetect=auto, translation=auto'
endif

ifdef I_FS_NONE
bochs:
	$(BOCHS) -q -f bochsrc.bxrc 'ata0-master: type=disk, path=hdd.img, mode=flat, cylinders=32, heads=16, spt=63, sect_size=512, model="Generic 1234", biosdetect=auto, translation=auto'
endif

qemu: $(FLOPPY_DISK)
	$(QEMU) -fda $^ $(QEMU_ARGUMENTS)

qemu-debug: $(FLOPPY_DISK)
	$(QEMU) -fda $^ $(QEMU_DEBUG_ARGUMENTS)

$(V).SILENT:
