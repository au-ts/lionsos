#
# Copyright 2026, LionsOS Contributors
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Host-side unit tests for the parts of LionsOS that are pure logic.
#
# LionsOS CI is build-only (ci/README.md), which cannot catch silent memory
# corruption. The code exercised here is the part of the filesystem client and
# server that has no Microkit dependency in its logic, so it can be compiled for
# the host and run under the address sanitizer.
#
# Run from anywhere:  make -C test
#
# To add a test for another component: write test_<area>.c, add its sources and an
# explicit rule below, and list the binary in `run`. Stub only the external symbols
# the component actually needs in include/, and never put include/ on a target
# build's include path.

LIONSOS := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

CC ?= cc
BUILD_DIR ?= $(LIONSOS)/test/build

# test/include must come first so the stub <microkit.h> wins over any real one.
# dep/sddf/include supplies <sddf/resources/common.h>, which is host-safe as-is.
INCLUDES := \
	-I$(LIONSOS)/test/include \
	-I$(LIONSOS)/include \
	-I$(LIONSOS)/dep/sddf/include

CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter -g $(INCLUDES)

# The out-of-bounds accesses and slot overruns these tests exist to catch are silent
# without a sanitizer, so it is not optional.
SANITIZERS := -fsanitize=address,undefined -fno-omit-frame-pointer

HELPERS_SRC := $(LIONSOS)/lib/fs/helpers/helpers.c
MEMORY_SRC := $(LIONSOS)/lib/fs/server/memory.c

HELPERS_BIN := $(BUILD_DIR)/test_fs_helpers
MEMORY_BIN := $(BUILD_DIR)/test_fs_server_memory
SIZE_BIN := $(BUILD_DIR)/test_gui_size
RELAYOUT_BIN := $(BUILD_DIR)/test_gui_relayout

all: run

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Explicit rules rather than a pattern: target-specific variables are not in scope
# when make expands a pattern rule's prerequisites, which silently leaves the binary
# with no dependency on the component source and so never rebuilds.
$(HELPERS_BIN): test_fs_helpers.c $(HELPERS_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) $(HELPERS_SRC) $< -o $@

$(MEMORY_BIN): test_fs_server_memory.c $(MEMORY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) $(MEMORY_SRC) $< -o $@

# No component source: the size limits are static inline in the protocol header,
# which is the point of them living there.
$(SIZE_BIN): test_gui_size.c $(LIONSOS)/include/lions/gui/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) $< -o $@

# gfx.c is freestanding, so the desktop's drawing code compiles for the host.
# Only src/ is needed on the include path, and never on a target build's.
$(RELAYOUT_BIN): test_gui_relayout.c $(LIONSOS)/examples/desktop/src/gfx.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) -I$(LIONSOS)/examples/desktop/src \
		$(LIONSOS)/examples/desktop/src/gfx.c $< -o $@

run: $(HELPERS_BIN) $(MEMORY_BIN) $(SIZE_BIN) $(RELAYOUT_BIN)
	@echo
	@for t in $(HELPERS_BIN) $(MEMORY_BIN) $(SIZE_BIN) $(RELAYOUT_BIN); do \
		echo "=== $$(basename $$t) ==="; \
		ASAN_OPTIONS=detect_leaks=0 "$$t" || exit 1; \
		echo; \
	done
	@echo "All host tests passed."

# Same tests without the sanitizers, for inspecting a failure by hand.
nosan: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HELPERS_SRC) test_fs_helpers.c -o $(BUILD_DIR)/nosan_fs_helpers
	$(CC) $(CFLAGS) $(MEMORY_SRC) test_fs_server_memory.c -o $(BUILD_DIR)/nosan_fs_server_memory

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run nosan clean