ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KERNEL_DIR := $(ROOT_DIR)/kernel
BUILD_DIR := $(ROOT_DIR)/build
ISO_DIR := $(BUILD_DIR)/iso
EFI_IMAGE := $(BUILD_DIR)/efi.img
ISO_IMAGE := $(BUILD_DIR)/liveos.iso
BOOT_CONFIG := $(ROOT_DIR)/boot.cfg
LOADER_EFI := $(ROOT_DIR)/EFI/edk2/Build/MyLoader/DEBUG_GCC/X64/MyLoader.efi

CC = x86_64-linux-gnu-gcc
LD = ld
CXX = x86_64-linux-gnu-g++
ASM = nasm

CFLAGS = -m64 -ffreestanding -nostdlib \
         -fno-stack-protector -fno-omit-frame-pointer \
         -mno-red-zone -fcf-protection=none \
         -mcmodel=kernel -fno-pic -fno-pie \
         -Wall -Wextra -g \
		 -Ikernel \
         -Ikernel/includes -Ikernel/includes/std -Ikernel/drivers -Ikernel/mm \
		 -Ikernel/arch -Ikernel/devices -Ikernel/filesystems \
		 -Ikernel/elf_loader -Ikernel/core -Ikernel/graphics

#CFLAGS += -DDEBUG

CXXFLAGS = -m64 -ffreestanding -nostdlib++ \
		   -fno-exceptions -fno-rtti -fno-threadsafe-statics \
		   -fno-use-cxa-atexit -fno-stack-protector \
		   -fno-omit-frame-pointer -mno-red-zone \
		   -fcf-protection=none -mcmodel=kernel -fno-pic -fno-pie \
		   -Wall -Wextra -g -std=c++20 \
		   -Ikernel/includes -Ikernel/includes/std \
		   -Ikernel/arch -Ikernel/mm -Ikernel \
		   -Ikernel/devices -Ikernel/filesystems -Ikernel/elf_loader \
		   -Ikernel/drivers -Ikernel/drivers/ops -Ikernel/core \
		   -Ikernel/graphics

LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib -static

C_FILES = $(shell find $(KERNEL_DIR) -name "*.c")
CXX_FILES = $(shell find $(KERNEL_DIR) -name "*.cpp")
ASM_FILES = $(shell find $(KERNEL_DIR) -name "*.S")
NASM_FILES = $(shell find $(KERNEL_DIR) -name "*.asm")
C_OBJ = $(patsubst $(ROOT_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_FILES))
CXX_OBJ = $(patsubst $(ROOT_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CXX_FILES))
ASM_OBJ = $(patsubst $(ROOT_DIR)/%.S,$(BUILD_DIR)/%.o,$(ASM_FILES))
NASM_OBJ = $(patsubst $(ROOT_DIR)/%.asm,$(BUILD_DIR)/%.o,$(NASM_FILES))
HEADERS = $(shell find $(KERNEL_DIR) -name "*.h")
HEADERS += $(shell find $(KERNEL_DIR) -name "*.hpp")
ALL_OBJ = $(C_OBJ) $(CXX_OBJ) $(ASM_OBJ) $(NASM_OBJ)

-include $(C_OBJ:.o=.d)

all: $(ISO_IMAGE)

$(BUILD_DIR)/kernel.elf: $(ALL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Kernel linked"

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.c $(ROOT_DIR)/Makefile $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.cpp $(ROOT_DIR)/Makefile $(HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.asm
	mkdir -p $(dir $@)
	$(ASM) -f elf64 -o $@ $<

$(LOADER_EFI):
	cd EFI/edk2/ && ./edksetup.sh && build -p MyLoader/MyLoader.dsc -a X64

$(ISO_IMAGE): $(BUILD_DIR)/kernel.elf $(LOADER_EFI) $(BOOT_CONFIG)
	rm -rf $(ISO_DIR) $(EFI_IMAGE)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	
	dd if=/dev/zero of=$(EFI_IMAGE) bs=1M count=33 2>/dev/null
	mkfs.fat -F32 $(EFI_IMAGE) 2>/dev/null
	mmd -i $(EFI_IMAGE) ::/EFI ::/EFI/BOOT 2>/dev/null
	
	mcopy -i $(EFI_IMAGE) $(LOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(EFI_IMAGE) $(BUILD_DIR)/kernel.elf ::/
	mcopy -i $(EFI_IMAGE) $(BOOT_CONFIG) ::/boot.cfg
	
	cp $(LOADER_EFI) $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/kernel.elf $(ISO_DIR)/
	cp $(BOOT_CONFIG) $(ISO_DIR)/boot.cfg
	cp $(EFI_IMAGE) $(ISO_DIR)/

	xorriso -as mkisofs \
		-R -J -V "MYLIVEOS" \
		-e efi.img \
		-no-emul-boot \
		-boot-load-size 4 \
		-o $@ \
		$(ISO_DIR)/
	
	@echo "✅ ISO created: $@"


run: $(ISO_IMAGE)
	bash user/build.sh --install-disk
	qemu-system-x86_64 \
		-cdrom $< \
		-m 256 \
		-serial stdio \
		-bios /usr/share/OVMF/x64/OVMF.4m.fd \
		-no-reboot \
		-no-shutdown \
		-drive file=disk.img,format=raw,if=none,id=nvme0 \
	        -device nvme,serial=deadbeef,drive=nvme0 \
		-netdev user,id=net0 -device e1000,netdev=net0

run-kvm: $(ISO_IMAGE)
	bash user/build.sh --install-disk
	qemu-system-x86_64 \
		-cdrom $< \
		-m 256 \
		-enable-kvm \
		-cpu host \
		-serial stdio \
		-bios /usr/share/OVMF/x64/OVMF.4m.fd \
		-no-reboot \
		-no-shutdown \
		-d int,cpu_reset,guest_errors \
		-drive file=disk.img,format=raw,if=none,id=nvme0 \
	        -device nvme,serial=deadbeef,drive=nvme0 \
		-netdev user,id=net0 -device e1000,netdev=net0

run-vbox: $(ISO_IMAGE)
	virtualboxvm --startvm liveos --debug

clean:
	rm -rf build mnt

list:
	@echo "C files: $(C_FILES)"
	@echo "Objects: $(ALL_OBJ)"

config-hive:
	python3 tools/hivegen.py tools/SYSTEM.ini tools/SYSTEM.HIV
	mcopy -o -i disk.img tools/SYSTEM.HIV ::/reg/SYSTEM.HIV

user-programs:
	bash user/build.sh

install-user-programs:
	bash user/build.sh --install-disk

test-net:
	gcc -std=c11 -Wall -Wextra -Wno-builtin-declaration-mismatch \
		-Ikernel -Ikernel/includes -Ikernel/includes/std -Ikernel/mm -Ikernel/drivers -Ikernel/arch \
		-o /tmp/liveos-net-stack-test tests/net_stack_test.c kernel/net/net.c kernel/net/netdev.c
	/tmp/liveos-net-stack-test

test-zip:
	gcc -std=c11 -Wall -Wextra -Iuser -o /tmp/liveos-zip-test tests/zip_test.c user/zip.c
	/tmp/liveos-zip-test

test-tls:
	gcc -std=c11 -Wall -Wextra -Werror -Iuser \
		-o /tmp/liveos-tls13-test tests/tls13_test.c user/tls13.c
	/tmp/liveos-tls13-test

.PHONY: all run clean list repo-index cdrom-repo user-programs install-user-programs config-hive test-net test-zip test-tls