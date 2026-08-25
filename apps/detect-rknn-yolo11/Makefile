# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2025-2026 @paxx12

TARGET = detect-rknn-yolo11.py
SRC = detect-rknn-yolo11.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all:
	@echo "Nothing to build for $(TARGET)"

clean:
	@echo "Nothing to clean for $(TARGET)"

install:
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(SRC) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
