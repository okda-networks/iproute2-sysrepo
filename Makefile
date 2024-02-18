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

EXEC = yp_sr
BIN  = bin

CC        ?= gcc
LDFLAGS   = -lsysrepo -lbpf -lelf -lmnl -lbsd -lcap -lselinux -lm
SUBDIRS   = iproute2

all: yp_sr.c iproute2/config.mk
	@set -e; \
	for i in $(SUBDIRS); do \
	$(MAKE) -C $$i || exit 1; done && \
	echo "Building $(BIN)/$(EXEC)" && \
	rm iproute2/ip/rtmon.o
	rm iproute2/ip/ip.o
	$(CC) -c yp_sr.c -Iiproute2/ip -Iiproute2/include
	$(CC) -o $(BIN)/$(EXEC) yp_sr.o `find iproute2/ip -name '*.[o]'` `find iproute2/lib -name '*.[o]'` $(LDFLAGS)
	@echo ""
	@echo "Make complete"

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
	$(MAKE) -C $$i clean || exit 1; done && \
	rm -f iproute2/config.mk && \
	rm -f yp_sr.o && \
	rm -f $(BIN)/$(EXEC)

iproute2/config.mk:
	@set -e; \
	cd iproute2 && \
	./configure && \
	cd ..

