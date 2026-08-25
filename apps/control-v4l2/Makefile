# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2026 @justinh-rahb

TARGET = control-v4l2.py
SRC = control-v4l2.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

clean:
	rm -f $(TARGET)

$(TARGET): $(SRC)
	cp $(SRC) $(TARGET)
	chmod +x $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
