TARGET = capture-pycamera2.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all:
	@echo "Nothing to build for $(TARGET)"

clean:
	@echo "Nothing to clean for $(TARGET)"

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
