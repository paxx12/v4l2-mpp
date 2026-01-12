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
