# SPDX-License-Identifier: GPL-3.0-or-later
ifneq ($(filter test host-test host-cli clean,$(MAKECMDGOALS)),)
HOST_ONLY := 1
endif

ifndef HOST_ONLY
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to the installed SDK, or use 'make test')
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

HOST_CC ?= cc
HOST_SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer
WARNINGS := -Wall -Wextra -Werror -Wpedantic
PAYLOAD_FLAGS := $(WARNINGS) -std=c11 -O2 -g -Iinclude
PAYLOAD_FLAGS += -ffile-prefix-map="$(CURDIR)"=. -fdebug-prefix-map="$(PS5_PAYLOAD_SDK)"=ps5-payload-sdk
SOURCES := src/main.c src/blocker.c
HEADERS := include/blocker.h
ELFS := dist/Status.elf dist/Disable-Updates.elf dist/Enable-Updates.elf

.PHONY: all test host-test host-cli clean
all: $(ELFS)

dist/Status.elf: $(SOURCES) $(HEADERS)
	mkdir -p dist
	$(CC) $(PAYLOAD_FLAGS) -DUB_ACTION=0 -o $@ $(SOURCES)

dist/Disable-Updates.elf: $(SOURCES) $(HEADERS)
	mkdir -p dist
	$(CC) $(PAYLOAD_FLAGS) -DUB_ACTION=1 -o $@ $(SOURCES)

dist/Enable-Updates.elf: $(SOURCES) $(HEADERS)
	mkdir -p dist
	$(CC) $(PAYLOAD_FLAGS) -DUB_ACTION=2 -o $@ $(SOURCES)

build/host-cli: src/blocker.c tests/host_cli.c $(HEADERS)
	mkdir -p build
	$(HOST_CC) $(WARNINGS) -std=c11 -O1 -g $(HOST_SANITIZE) -DUB_HOST_TEST=1 -Iinclude -o $@ src/blocker.c tests/host_cli.c

host-cli: build/host-cli
host-test test: build/host-cli
	python3 tests/test_filesystem.py build/host-cli

clean:
	rm -f $(ELFS) build/host-cli
