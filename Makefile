# SPDX-License-Identifier: AGPL-3.0-or-later 
# Authors:     Vincent Jardin, <vjardin@free.fr>
#
#              This program is free software; you can redistribute it and/or
#              modify it under the terms of the GNU Affero General Public
#              License Version 3.0 as published by the Free Software Foundation;
#              either version 3.0 of the License, or (at your option) any later
#              version.
#
# Copyright (C) 2024 Vincent Jardin, <vjardin@free.fr>
#

EXEC = iproute2-sysrepo
BIN  = bin

CC        ?= gcc
LDFLAGS   = -lyang -lsysrepo -lbpf -lelf -lmnl -lbsd -lcap -lselinux -lm -ldl -ljson-c -rdynamic -L/usr/local/lib
SUBDIRS   = iproute2

IPR2_SR_LIB_SRC = $(wildcard src/lib/*.c)
IPR2_SR_SRC = $(wildcard src/*.c)

IPR2_SR_LIB_OBJ = $(IPR2_SR_LIB_SRC:.c=.o)
IPR2_SR_OBJ = $(IPR2_SR_SRC:.c=.o)


all: $(IPR2_SR_OBJ) $(IPR2_SR_LIB_OBJ) iproute2/config.mk
	@echo "Checking if json_print patch is already applied..."
	@if ! patch --dry-run --reverse --force -d iproute2 -p1 < ipr2_patches/json_print.patch; then \
		echo "Applying json_print patch..."; \
		patch -d iproute2 -p1 < ipr2_patches/json_print.patch || { echo "Patch failed"; true; }; \
	else \
		echo "json_print patch is already applied, skipping..."; \
	fi
	@set -e; \
	for i in $(SUBDIRS); do \
	$(MAKE) -C $$i || exit 1; done && \
	echo "Building $(BIN)/$(EXEC)" && \
	rm iproute2/ip/rtmon.o
	rm iproute2/ip/ip.o
	rm iproute2/bridge/bridge.o
	rm iproute2/tc/tc.o
	@echo ""
	@echo "Patching conflicting symbols"
	objcopy --redefine-sym print_linkinfo=br_print_linkinfo iproute2/bridge/link.o
	objcopy --redefine-sym print_linkinfo=br_print_linkinfo iproute2/bridge/monitor.o
	@echo ""
	$(CC) -o $(BIN)/$(EXEC) $(IPR2_SR_OBJ) $(IPR2_SR_LIB_OBJ) `find iproute2/ip -name '*.[o]'` `find iproute2/bridge -name '*.[o]'` `find iproute2/tc -name '*.[o]'` `find iproute2/lib -name '*.[o]'` $(LDFLAGS)
	@echo ""
	@echo "Make complete"

clean:
	@echo "Checking if json_print patch is already applied..."
	@if ! patch --dry-run --reverse --force -d iproute2 -p1 < ipr2_patches/json_print.patch; then \
		echo "Patch wasn't applying, nothing to reverse..."; \
	else \
		echo "Reversing json_print patch..."; \
		patch -R -d iproute2 -p1 < ipr2_patches/json_print.patch || { echo "Patch failed"; true; }; \
	fi
	@set -e; \
	for i in $(SUBDIRS); do \
	$(MAKE) -C $$i clean || exit 1; done && \
	rm -f iproute2/config.mk && \
	rm -f $(IPR2_SR_OBJ) && \
	rm -f $(IPR2_SR_LIB_OBJ) && \
	rm -f $(BIN)/$(EXEC)

clean_srmod:
	@set -e; \
	rm -f $(IPR2_SR_OBJ) && \
	rm -f $(IPR2_SR_LIB_OBJ) && \
	rm -f $(BIN)/$(EXEC)

iproute2/config.mk:
	@set -e; \
	cd iproute2 && \
	./configure && \
	cd ..

check:
	yanglint yang/*.yang

src/lib/%.o: src/lib/%.c
	$(CC) -c $< -o $@ -Iiproute2/ip -Iiproute2/bridge -Iiproute2/tc -Iiproute2/include


src/%.o: $(IPR2_SR_SRC) $(IPR2_SR_LIB_OBJ)
	$(CC) -c $< -o $@ -Iiproute2/ip -Iiproute2/bridge -Iiproute2/tc -Iiproute2/include

