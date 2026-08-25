# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2025-2026 @paxx12, @justinh-rahb

TARGET = stream-http.py
SRC = stream-http.py
HTML_FILES = html/index.html html/player.html html/webrtc.html html/control.html

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
HTMLDIR = /usr/share/stream-http/html

all:
	@echo "Nothing to build for $(TARGET)"

clean:
	@echo "Nothing to clean for $(TARGET)"

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(HTMLDIR)
	install -m 644 $(HTML_FILES) $(DESTDIR)$(HTMLDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)/usr/share/stream-http

.PHONY: all clean install uninstall
