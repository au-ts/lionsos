#
# Copyright 2026, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Builds the fs client helpers for the host and runs them under the
# address and undefined-behaviour sanitisers.
#

LIONSOS := $(abspath ../../../..)
CC ?= cc
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-I$(LIONSOS)/include -Istub

test_helpers: test_helpers.c $(LIONSOS)/lib/fs/helpers/helpers.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: check clean
check: test_helpers
	./test_helpers

clean:
	rm -f test_helpers
