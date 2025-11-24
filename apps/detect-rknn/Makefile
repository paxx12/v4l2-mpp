TARGET = detect-rknn.py
SRC = main.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all:
	@echo "Nothing to build for $(TARGET)"

clean:
	@echo "Nothing to clean for $(TARGET)"

rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl:
	wget -O $@ https://github.com/airockchip/rknn-toolkit2/raw/refs/heads/master/rknn-toolkit-lite2/packages/$@

RKNN_TOOLKIT_FILENAME := rknn_toolkit_lite2-2.3.2-cp311-cp311-manylinux_2_17_aarch64.manylinux2014_aarch64.whl

install: $(RKNN_TOOLKIT_FILENAME)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(SRC) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 755 $(RKNN_TOOLKIT_FILENAME) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
