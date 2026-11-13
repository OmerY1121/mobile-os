CC := gcc
CFLAGS := -static -O2 -Wall -Wextra -Werror

BUILD := build
ROOTFS := $(BUILD)/rootfs
INITRAMFS := $(BUILD)/initramfs.cpio.gz
KERNEL ?= $(BUILD)/vmlinuz

.PHONY: all run clean test

all: $(INITRAMFS)

$(BUILD):
	mkdir -p $@

$(BUILD)/init: src/init.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/statusd: src/statusd.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/statusctl: src/statusctl.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(INITRAMFS): $(BUILD)/init $(BUILD)/statusd $(BUILD)/statusctl Makefile
	rm -rf $(ROOTFS)
	mkdir -p $(ROOTFS)/bin $(ROOTFS)/proc $(ROOTFS)/sys $(ROOTFS)/dev $(ROOTFS)/run/mobile-os
	cp $(BUILD)/init $(ROOTFS)/init
	cp $(BUILD)/statusd $(BUILD)/statusctl $(ROOTFS)/bin/
	fakeroot sh -ec 'cd "$(ROOTFS)"; mknod -m 600 dev/console c 5 1; mknod -m 666 dev/null c 1 3; find . -print0 | cpio --null --quiet -o -H newc | gzip -9 > ../initramfs.cpio.gz'

run: all
	@test -r "$(KERNEL)" || { echo "Kernel image missing: $(KERNEL)" >&2; exit 1; }
	qemu-system-x86_64 \
		-m 512 \
		-accel tcg \
		-nographic \
		-nic none \
		-kernel "$(KERNEL)" \
		-initrd "$(INITRAMFS)" \
		-append "console=ttyS0 rdinit=/init" \
		-no-reboot

clean:
	rm -rf $(BUILD)/init $(BUILD)/statusd $(BUILD)/statusctl $(ROOTFS) $(INITRAMFS)

test: $(BUILD)/statusd $(BUILD)/statusctl
	bash tests/test-ipc.sh
