TARGET = stream-snap-mqtt.py
SRC = main.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all:
	@echo "Nothing to build for $(TARGET)"

clean:
	@echo "Nothing to clean for $(TARGET)"

install:
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(SRC) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 755 ctrl.sh $(DESTDIR)$(BINDIR)/stream-snap-mqtt-ctrl.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/stream-snap-mqtt-ctrl.sh

.PHONY: all clean install uninstall
