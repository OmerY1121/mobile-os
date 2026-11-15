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

$(BUILD)/init: src/init.c src/log_client.h src/service_state.c src/service_state.h VERSION src/power_control.h src/power_control.c src/power_socket.h src/power_socket.c src/ipc_io.h src/shutdown_state.h src/shutdown_state.c src/power_transition.h src/power_transition.c | $(BUILD) src/service_config.c src/service_config.h
	$(CC) $(CFLAGS) -DSELYRA_VERSION='"$(shell cat VERSION)"' -o $@ $< src/service_config.c src/service_state.c src/power_control.c src/power_socket.c src/shutdown_state.c src/power_transition.c

$(BUILD)/init-dry-run: src/init.c src/log_client.h src/service_state.c src/service_state.h VERSION src/power_control.h src/power_control.c src/power_socket.h src/power_socket.c src/ipc_io.h src/shutdown_state.h src/shutdown_state.c src/power_transition.h src/power_transition.c | $(BUILD) src/service_config.c src/service_config.h
	$(CC) $(CFLAGS) -DSELYRA_POWER_DRY_RUN -DSELYRA_VERSION='"$(shell cat VERSION)"' -o $@ $< src/service_config.c src/service_state.c src/power_control.c src/power_socket.c src/shutdown_state.c src/power_transition.c

$(BUILD)/statusd: src/statusd.c src/ipc_io.h src/log_client.h src/memory_info.h src/cpu_info.h src/cpu_info.c src/system_info.h VERSION | $(BUILD)
	$(CC) $(CFLAGS) -DSELYRA_VERSION='"$(shell cat VERSION)"' -o $@ $< src/cpu_info.c

$(BUILD)/statusctl: src/statusctl.c src/ipc_io.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(INITRAMFS): $(BUILD)/init $(BUILD)/statusd $(BUILD)/statusctl $(BUILD)/powerctl Makefile $(BUILD)/logd $(BUILD)/logctl config/services.conf
	rm -rf $(ROOTFS)
	mkdir -p $(ROOTFS)/bin $(ROOTFS)/proc $(ROOTFS)/sys $(ROOTFS)/dev $(ROOTFS)/run/mobile-os $(ROOTFS)/var/lib/mobile-os $(ROOTFS)/tmp
	cp $(BUILD)/init $(ROOTFS)/init
	cp $(BUILD)/statusd $(BUILD)/statusctl $(BUILD)/powerctl $(ROOTFS)/bin/
	cp $(BUILD)/logd $(BUILD)/logctl $(ROOTFS)/bin/
	install -D -m 0600 config/services.conf $(ROOTFS)/etc/mobile-os/services.conf
	fakeroot sh -ec 'cd "$(ROOTFS)"; chown -R 0:0 etc; chmod 0600 etc/mobile-os/services.conf; mknod -m 600 dev/console c 5 1; mknod -m 666 dev/null c 1 3; find . -print0 | cpio --null --quiet -o -H newc | gzip -9 > ../initramfs.cpio.gz'

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
	rm -rf $(BUILD)/init $(BUILD)/statusd $(BUILD)/statusctl $(BUILD)/powerctl $(BUILD)/test-powerctl $(ROOTFS) $(INITRAMFS) $(BUILD)/logd $(BUILD)/logctl

test: $(BUILD)/statusd $(BUILD)/statusctl
	bash tests/test-ipc.sh


.PHONY: test-boot
test-boot:
	python3 tests/test-qemu-boot.py

.PHONY: test-restart
test-restart:
	bash tests/test-qemu-restart.sh


.PHONY: test-stop
test-stop:
	bash tests/test-qemu-stop.sh

.PHONY: test-all
test-all:
	$(MAKE) test
	$(MAKE) test-boot
	$(MAKE) test-restart
	$(MAKE) test-stop

.PHONY: test-arm-boot
test-arm-boot:
	python3 tests/test-armv7-boot.py


.PHONY: package-s3
package-s3:
	bash scripts/package-s3-candidate.sh

.PHONY: log-tools test-logs

log-tools: build/logd build/logctl

build/logd: src/logd.c
	@mkdir -p build
	$(CC) -static -O2 -Wall -Wextra -Werror -o $@ $<

build/logctl: src/logctl.c
	@mkdir -p build
	$(CC) -static -O2 -Wall -Wextra -Werror -o $@ $<

test-logs: log-tools
	bash tests/test-logs.sh


.PHONY: test-memory
test-memory:
	$(CC) $(CFLAGS) -o $(BUILD)/test-memory-info tests/test-memory-info.c
	./$(BUILD)/test-memory-info

.PHONY: test-cpu
test-cpu: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test-cpu-info tests/test-cpu-info.c src/cpu_info.c
	./$(BUILD)/test-cpu-info

.PHONY: test-system
test-system: | $(BUILD)
	$(CC) $(CFLAGS) -DSELYRA_VERSION='"$(shell cat VERSION)"' -o $(BUILD)/test-system-info tests/test-system-info.c
	./$(BUILD)/test-system-info

.PHONY: check
check:
	$(MAKE) all
	$(MAKE) test-all
	$(MAKE) test-logs
	$(MAKE) test-memory
	$(MAKE) test-cpu
	$(MAKE) test-system
	$(MAKE) test-power
	$(MAKE) test-power-socket
	$(MAKE) test-powerctl
	$(MAKE) test-power-transition
	$(MAKE) test-shutdown-state

.PHONY: test-log-auth
test-log-auth:
	bash tests/test-qemu-log-auth.sh

.PHONY: test-log-auth

.PHONY: test-power
test-power: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test-power-control tests/test-power-control.c src/power_control.c
	./$(BUILD)/test-power-control

.PHONY: test-power-socket
test-power-socket: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test-power-socket tests/test-power-socket.c src/power_socket.c src/power_control.c
	./$(BUILD)/test-power-socket

.PHONY: test-shutdown-state
test-shutdown-state: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test-shutdown-state tests/test-shutdown-state.c src/shutdown_state.c
	./$(BUILD)/test-shutdown-state

$(BUILD)/powerctl: src/powerctl.c src/ipc_io.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/test-powerctl: src/powerctl.c src/ipc_io.h | $(BUILD)
	$(CC) $(CFLAGS) -DSELYRA_POWERCTL_TEST -o $@ $<

.PHONY: test-powerctl
test-powerctl: $(BUILD)/test-powerctl
	python3 tests/test-powerctl.py

.PHONY: test-qemu-power
test-qemu-power: all $(BUILD)/init-dry-run
	python3 tests/test-qemu-power.py

.PHONY: test-power-transition
test-power-transition: | $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test-power-transition tests/test-power-transition.c src/power_transition.c
	./$(BUILD)/test-power-transition

.PHONY: test-qemu-poweroff test-qemu-reboot

test-qemu-poweroff: all
	python3 tests/test-qemu-power-transition.py poweroff

test-qemu-reboot: all
	python3 tests/test-qemu-power-transition.py reboot

.PHONY: test-qemu-force-poweroff
test-qemu-force-poweroff: all
	python3 tests/test-qemu-power-transition.py poweroff --stubborn-logd
